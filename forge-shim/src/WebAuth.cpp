// Offline Xbox Live identity: replaces the Windows account broker (WebAuthenticationCoreManager)
// with a local provider that answers every token request with an "offline" Xbox Live sign-in.
// XSAPI (user_impl_idp) reads XboxUserId/Gamertag/AgeGroup/Environment/Sandbox from the response;
// with an empty Token it skips the online presence check and signs the user in locally.
// Env: FORGE_GAMERTAG (default "Spartan"), FORGE_XUID (default 2533274800000001).
#include <windows.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <roapi.h>
#include <windows.security.authentication.web.core.h>
#include <windows.security.credentials.h>
#include <windows.foundation.collections.h>
#include <string>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::Security::Credentials;
using namespace ABI::Windows::Security::Authentication::Web::Core;
namespace WAC = ABI::Windows::Security::Authentication::Web::Core;
namespace WSC = ABI::Windows::Security::Credentials;

static void Trace(const char* fmt, ...) { char b[600]; va_list a; va_start(a, fmt); vsprintf_s(b, fmt, a); va_end(a); char t[32]; sprintf_s(t, "[ForgeShim] WAM %6llu ", GetTickCount64() % 1000000); OutputDebugStringA(t); OutputDebugStringA(b); OutputDebugStringA("\n"); }
static std::wstring Env(const wchar_t* name, const wchar_t* fallback) { wchar_t v[128]; DWORD n = GetEnvironmentVariableW(name, v, 128); return (n && n < 128) ? std::wstring(v, n) : std::wstring(fallback); }
static HRESULT Str(const std::wstring& s, HSTRING* out) { if (!out) return E_POINTER; return WindowsCreateString(s.c_str(), static_cast<UINT32>(s.size()), out); }
static bool FullShape() { static bool f = Env(L"FORGE_WAM_SHAPE", L"offline") == L"full"; return f; }
bool RealTokensEnabled();
bool RealTokenFor(const std::wstring& url, std::wstring& authorization, std::wstring& xuid, std::wstring& gamertag, std::wstring& age, std::wstring& privileges);
static std::wstring FromH(HSTRING h) { UINT32 n = 0; auto p = WindowsGetStringRawBuffer(h, &n); return std::wstring(p ? p : L"", n); }

HRESULT CompatStringMap(IMap<HSTRING, HSTRING>** out);
static HRESULT NewStringMap(ComPtr<IMap<HSTRING, HSTRING>>& map) {
    ComPtr<IInspectable> obj; HRESULT hr = RoActivateInstance(HStringReference(L"Windows.Foundation.Collections.StringMap").Get(), &obj);
    if (SUCCEEDED(hr) && SUCCEEDED(obj.As(&map)) && map) return S_OK;
    // Wine (the PS5) has no StringMap; without one every token request's Properties is null and XSAPI crashes on it.
    map.Reset(); return CompatStringMap(map.ReleaseAndGetAddressOf());
}
static void Put(IMap<HSTRING, HSTRING>* m, const wchar_t* k, const std::wstring& v) { boolean replaced; m->Insert(HStringReference(k).Get(), HStringReference(v.c_str(), static_cast<unsigned>(v.size())).Get(), &replaced); }

#include <typeinfo>
#define WAM_NAME(n) public: HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* v) override { return WindowsCreateString(n, static_cast<UINT32>(wcslen(n)), v); } \
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* t) override { *t = BaseTrust; return S_OK; } private:
#define WAM_QI(...) public: HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override { \
    HRESULT hr = RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, __VA_ARGS__>::QueryInterface(iid, out); \
    if (FAILED(hr)) { wchar_t g[64]; StringFromGUID2(iid, g, 64); char b[160]; sprintf_s(b, "QI miss on %s for %ls", typeid(*this).name(), g); Trace("%s", b); } return hr; } private:

// ---- completed async operation -------------------------------------------------------------
template <typename TRuntime, typename TAbi>
class Done : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IAsyncOperation<TRuntime>, IAsyncInfo> {
    ComPtr<TAbi> value_; ComPtr<IAsyncOperationCompletedHandler<TRuntime>> handler_;
public:
    explicit Done(TAbi* v) : value_(v) {}
    HRESULT STDMETHODCALLTYPE put_Completed(IAsyncOperationCompletedHandler<TRuntime>* h) override { handler_ = h; return h ? h->Invoke(this, AsyncStatus::Completed) : S_OK; }
    HRESULT STDMETHODCALLTYPE get_Completed(IAsyncOperationCompletedHandler<TRuntime>** h) override { return handler_.CopyTo(h); }
    HRESULT STDMETHODCALLTYPE GetResults(TAbi** r) override { if (!r) return E_POINTER; return value_.CopyTo(r); }
    HRESULT STDMETHODCALLTYPE get_Id(unsigned* id) override { *id = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Status(AsyncStatus* s) override { *s = AsyncStatus::Completed; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* e) override { *e = S_OK; return S_OK; }
    HRESULT STDMETHODCALLTYPE Cancel() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
template <typename TRuntime, typename TAbi, typename TOut>
static HRESULT Complete(TAbi* value, TOut** out) {
    if (!out) return E_POINTER; auto op = Make<Done<TRuntime, TAbi>>(value); return op ? op.CopyTo(out) : E_OUTOFMEMORY;
}

// ---- provider / account ---------------------------------------------------------------------
class Provider : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IWebAccountProvider> {
    WAM_QI(IWebAccountProvider)
    WAM_NAME(L"Windows.Security.Credentials.WebAccountProvider")
    std::wstring id_;
public:
    explicit Provider(std::wstring id) : id_(std::move(id)) {}
    HRESULT STDMETHODCALLTYPE get_Id(HSTRING* v) override { return Str(id_, v); }
    HRESULT STDMETHODCALLTYPE get_DisplayName(HSTRING* v) override { return Str(L"Xbox Live (offline)", v); }
    HRESULT STDMETHODCALLTYPE get_IconUri(IUriRuntimeClass** v) override { if (v) *v = nullptr; return S_OK; }
};
class Account : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IWebAccount, IWebAccount2> {
    WAM_QI(IWebAccount, IWebAccount2)
    WAM_NAME(L"Windows.Security.Credentials.WebAccount")
    ComPtr<IWebAccountProvider> provider_;
public:
    explicit Account(IWebAccountProvider* p) : provider_(p) {}
    HRESULT STDMETHODCALLTYPE get_WebAccountProvider(IWebAccountProvider** v) override { Trace("Account.Provider"); return provider_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_UserName(HSTRING* v) override { return Str(Env(L"FORGE_GAMERTAG", L"Spartan"), v); }
    HRESULT STDMETHODCALLTYPE get_State(WebAccountState* v) override { Trace("Account.State"); *v = WebAccountState_Connected; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Id(HSTRING* v) override { return Str(L"forge-offline-account", v); }
    HRESULT STDMETHODCALLTYPE get_Properties(IMapView<HSTRING, HSTRING>** v) override { Trace("Account.Properties"); ComPtr<IMap<HSTRING, HSTRING>> m; HRESULT hr = NewStringMap(m); return SUCCEEDED(hr) ? m->GetView(v) : hr; }
    HRESULT STDMETHODCALLTYPE GetPictureAsync(WebAccountPictureSize, IAsyncOperation<ABI::Windows::Storage::Streams::IRandomAccessStream*>** v) override { if (v) *v = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SignOutAsync(IAsyncAction** v) override { Trace("Account.SignOutAsync"); if (v) *v = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SignOutWithClientIdAsync(HSTRING, IAsyncAction** v) override { if (v) *v = nullptr; return E_NOTIMPL; }
};

// ---- token request (our own, so the real class never sees our fake provider) -----------------
class Request : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IWebTokenRequest> {
    WAM_QI(IWebTokenRequest)
    WAM_NAME(L"Windows.Security.Authentication.Web.Core.WebTokenRequest")
    ComPtr<IWebAccountProvider> provider_; std::wstring scope_, client_; WebTokenRequestPromptType prompt_; ComPtr<IMap<HSTRING, HSTRING>> props_;
public:
    Request(IWebAccountProvider* p, std::wstring s, std::wstring c, WebTokenRequestPromptType t) : provider_(p), scope_(std::move(s)), client_(std::move(c)), prompt_(t) { NewStringMap(props_); }
    HRESULT STDMETHODCALLTYPE get_WebAccountProvider(IWebAccountProvider** v) override { return provider_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_Scope(HSTRING* v) override { return Str(scope_, v); }
    HRESULT STDMETHODCALLTYPE get_ClientId(HSTRING* v) override { return Str(client_, v); }
    HRESULT STDMETHODCALLTYPE get_PromptType(WebTokenRequestPromptType* v) override { *v = prompt_; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Properties(IMap<HSTRING, HSTRING>** v) override { return props_.CopyTo(v); }
};
class RequestFactory : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, IWebTokenRequestFactory> {
    WAM_QI(IActivationFactory, IWebTokenRequestFactory)
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** v) override { if (v) *v = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Create(IWebAccountProvider* p, HSTRING scope, HSTRING client, IWebTokenRequest** v) override { return MakeAndInitialize2(p, FromH(scope), FromH(client), WebTokenRequestPromptType_Default, v); }
    HRESULT STDMETHODCALLTYPE CreateWithPromptType(IWebAccountProvider* p, HSTRING scope, HSTRING client, WebTokenRequestPromptType t, IWebTokenRequest** v) override { return MakeAndInitialize2(p, FromH(scope), FromH(client), t, v); }
    HRESULT STDMETHODCALLTYPE CreateWithProvider(IWebAccountProvider* p, IWebTokenRequest** v) override { return MakeAndInitialize2(p, L"", L"", WebTokenRequestPromptType_Default, v); }
    HRESULT STDMETHODCALLTYPE CreateWithScope(IWebAccountProvider* p, HSTRING scope, IWebTokenRequest** v) override { return MakeAndInitialize2(p, FromH(scope), L"", WebTokenRequestPromptType_Default, v); }
private:
    static HRESULT MakeAndInitialize2(IWebAccountProvider* p, std::wstring s, std::wstring c, WebTokenRequestPromptType t, IWebTokenRequest** v) {
        if (!v) return E_POINTER; auto r = Make<Request>(p, std::move(s), std::move(c), t); return r ? r.CopyTo(v) : E_OUTOFMEMORY;
    }
};

// ---- response / result ----------------------------------------------------------------------
class Response : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IWebTokenResponse> {
    WAM_QI(IWebTokenResponse)
    WAM_NAME(L"Windows.Security.Authentication.Web.Core.WebTokenResponse")
    ComPtr<IWebAccount> account_; ComPtr<IMap<HSTRING, HSTRING>> props_; std::wstring token_;
public:
    Response(IWebAccount* a, IMap<HSTRING, HSTRING>* p, std::wstring token = L"") : account_(a), props_(p), token_(std::move(token)) {}
    // Empty token = offline sign-in: XSAPI skips its online presence validation and signs in locally.
    HRESULT STDMETHODCALLTYPE get_Token(HSTRING* v) override { if (!v) return E_POINTER; *v = nullptr; if (!token_.empty()) return Str(token_, v); return FullShape() ? Str(L"XBL3.0 x=0;offline", v) : S_OK; }
    HRESULT STDMETHODCALLTYPE get_ProviderError(IWebProviderError** v) override { if (v) *v = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_WebAccount(IWebAccount** v) override { return account_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_Properties(IMap<HSTRING, HSTRING>** v) override { return props_.CopyTo(v); }
};
class ResponseList : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IVectorView<WebTokenResponse*>> {
    WAM_QI(IVectorView<WebTokenResponse*>)
    WAM_NAME(L"ForgeShim.WebTokenResponseList")
    ComPtr<IWebTokenResponse> item_;
public:
    explicit ResponseList(IWebTokenResponse* r) : item_(r) {}
    HRESULT STDMETHODCALLTYPE GetAt(unsigned i, IWebTokenResponse** v) override { if (i != 0) return E_BOUNDS; return item_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* n) override { *n = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE IndexOf(IWebTokenResponse* r, unsigned* i, boolean* found) override { *i = 0; *found = r == item_.Get(); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMany(unsigned start, unsigned cap, IWebTokenResponse** items, unsigned* got) override {
        *got = 0; if (start == 0 && cap > 0) { item_.CopyTo(&items[0]); *got = 1; } return S_OK;
    }
};
class Result : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IWebTokenRequestResult> {
    WAM_QI(IWebTokenRequestResult)
    WAM_NAME(L"Windows.Security.Authentication.Web.Core.WebTokenRequestResult")
    ComPtr<IVectorView<WebTokenResponse*>> data_;
public:
    explicit Result(IVectorView<WebTokenResponse*>* d) : data_(d) {}
    HRESULT STDMETHODCALLTYPE get_ResponseData(IVectorView<WebTokenResponse*>** v) override { return data_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_ResponseStatus(WebTokenRequestStatus* v) override { *v = WebTokenRequestStatus_Success; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ResponseError(IWebProviderError** v) override { if (v) *v = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE InvalidateCacheAsync(IAsyncAction** v) override { if (v) *v = nullptr; return E_NOTIMPL; }
};

static ComPtr<IWebAccountProvider> XboxProvider(const std::wstring& id) { return Make<Provider>(id); }

volatile long g_offlineTokenRequests = 0;
static HRESULT MakeResult(IWebTokenRequest* request, IAsyncOperation<WebTokenRequestResult*>** out) {
    InterlockedIncrement(&g_offlineTokenRequests);
    std::wstring url, scope; ComPtr<IWebAccountProvider> provider;
    if (request) {
        HString s; request->get_Scope(s.GetAddressOf()); scope = FromH(s.Get());
        request->get_WebAccountProvider(&provider);
        ComPtr<IMap<HSTRING, HSTRING>> p; if (SUCCEEDED(request->get_Properties(&p)) && p) { HString u; if (SUCCEEDED(p->Lookup(HStringReference(L"Url").Get(), u.GetAddressOf()))) url = FromH(u.Get()); }
    }
    if (!provider) provider = XboxProvider(L"https://xsts.auth.xboxlive.com");
    ComPtr<IMap<HSTRING, HSTRING>> props; HRESULT hr = NewStringMap(props); if (FAILED(hr)) return hr;
    std::wstring realToken, realXuid, realTag, realAge, realPrivs;
    bool real = RealTokensEnabled() && RealTokenFor(url, realToken, realXuid, realTag, realAge, realPrivs);
    if (real) {   // FORGE_REAL_TOKENS: the signed-in account's own XSTS token for this Url's relying party
        Put(props.Get(), L"XboxUserId", realXuid); Put(props.Get(), L"Gamertag", realTag); Put(props.Get(), L"AgeGroup", realAge.empty() ? std::wstring(L"Adult") : realAge);
        Put(props.Get(), L"Environment", L"prod"); Put(props.Get(), L"Sandbox", L"RETAIL"); Put(props.Get(), L"Privileges", realPrivs);
        auto account = Make<Account>(provider.Get());
        auto response = Make<Response>(account.Get(), props.Get(), realToken);
        auto list = Make<ResponseList>(response.Get());
        auto result = Make<Result>(list.Get());
        static volatile long realRequests = 0; if (InterlockedIncrement(&realRequests) <= 50) Trace("token request url=%.200ls -> real user %ls (%ls)", url.c_str(), realTag.c_str(), realXuid.c_str());
        return Complete<WebTokenRequestResult*, IWebTokenRequestResult>(result.Get(), out);
    }
    std::wstring gamertag = Env(L"FORGE_GAMERTAG", L"Spartan"), xuid = Env(L"FORGE_XUID", L"2533274800000001");
    Put(props.Get(), L"XboxUserId", xuid);
    Put(props.Get(), L"Gamertag", gamertag);
    Put(props.Get(), L"AgeGroup", L"Adult");
    Put(props.Get(), L"Environment", L"prod");
    Put(props.Get(), L"Sandbox", L"RETAIL");
    if (FullShape()) {   // online-shaped response: privileges + signature present
        std::wstring privs; for (int p = 185; p <= 255; ++p) { privs += std::to_wstring(p); privs += L' '; } privs.pop_back();
        Put(props.Get(), L"Privileges", privs);
        Put(props.Get(), L"Signature", L"AAAAAQHUAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
    }
    auto account = Make<Account>(provider.Get());
    auto response = Make<Response>(account.Get(), props.Get());
    auto list = Make<ResponseList>(response.Get());
    auto result = Make<Result>(list.Get());
    Trace("token request scope=%ls url=%ls -> offline user %ls (%ls)", scope.c_str(), url.c_str(), gamertag.c_str(), xuid.c_str());
    return Complete<WebTokenRequestResult*, IWebTokenRequestResult>(result.Get(), out);
}

class Manager : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, IWebAuthenticationCoreManagerStatics, IWebAuthenticationCoreManagerStatics2> {
    WAM_QI(IActivationFactory, IWebAuthenticationCoreManagerStatics, IWebAuthenticationCoreManagerStatics2)
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** v) override { if (v) *v = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetTokenSilentlyAsync(IWebTokenRequest* r, IAsyncOperation<WebTokenRequestResult*>** v) override { return MakeResult(r, v); }
    HRESULT STDMETHODCALLTYPE GetTokenSilentlyWithWebAccountAsync(IWebTokenRequest* r, IWebAccount*, IAsyncOperation<WebTokenRequestResult*>** v) override { return MakeResult(r, v); }
    HRESULT STDMETHODCALLTYPE RequestTokenAsync(IWebTokenRequest* r, IAsyncOperation<WebTokenRequestResult*>** v) override { return MakeResult(r, v); }
    HRESULT STDMETHODCALLTYPE RequestTokenWithWebAccountAsync(IWebTokenRequest* r, IWebAccount*, IAsyncOperation<WebTokenRequestResult*>** v) override { return MakeResult(r, v); }
    HRESULT STDMETHODCALLTYPE FindAccountAsync(IWebAccountProvider* p, HSTRING id, IAsyncOperation<WebAccount*>** v) override {
        Trace("FindAccountAsync %ls", FromH(id).c_str()); auto a = Make<Account>(p); return Complete<WebAccount*, IWebAccount>(a.Get(), v);
    }
    HRESULT STDMETHODCALLTYPE FindAccountProviderAsync(HSTRING id, IAsyncOperation<WebAccountProvider*>** v) override {
        Trace("FindAccountProviderAsync %ls", FromH(id).c_str()); auto p = XboxProvider(FromH(id)); return Complete<WebAccountProvider*, IWebAccountProvider>(p.Get(), v);
    }
    HRESULT STDMETHODCALLTYPE FindAccountProviderWithAuthorityAsync(HSTRING id, HSTRING, IAsyncOperation<WebAccountProvider*>** v) override { return FindAccountProviderAsync(id, v); }
    HRESULT STDMETHODCALLTYPE FindAccountProviderWithAuthorityForUserAsync(HSTRING id, HSTRING, ABI::Windows::System::IUser*, IAsyncOperation<WebAccountProvider*>** v) override { return FindAccountProviderAsync(id, v); }
};

HRESULT WebAuthManagerActivation(REFIID iid, void** out) { static ComPtr<Manager> m = Make<Manager>(); return m ? m.CopyTo(iid, out) : E_OUTOFMEMORY; }
HRESULT WebTokenRequestActivation(REFIID iid, void** out) { static ComPtr<RequestFactory> f = Make<RequestFactory>(); return f ? f.CopyTo(iid, out) : E_OUTOFMEMORY; }
