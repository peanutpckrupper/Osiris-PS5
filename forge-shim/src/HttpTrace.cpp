// FORGE_TRACE_HTTP=1: log the game's Xbox Live / Halo HTTP traffic (cpprest on UWP -> IXMLHTTPRequest2).
// Hooks IXMLHTTPRequest2::Open/SetRequestHeader/Send in msxml6 and wraps the status callback to log the
// response status, plus x-err / WWW-Authenticate and the start of error bodies. Authorization and
// Signature values are never logged, only whether they were present.
#include <windows.h>
#include <msxml6.h>
#include <roapi.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.networking.sockets.h>
#include <wrl.h>
#include <string>
#include <map>
#include <mutex>
#include <cstdio>
#include <memory>
#include "../third_party/minhook/include/MinHook.h"
using Microsoft::WRL::ComPtr;

namespace {
void Trace(const char* fmt, ...) { char b[1400]; va_list a; va_start(a, fmt); _vsnprintf_s(b, _TRUNCATE, fmt, a); va_end(a); OutputDebugStringA("[ForgeShim] HTTP "); OutputDebugStringA(b); OutputDebugStringA("\n"); }
std::string Narrow(const wchar_t* w) { if (!w) return ""; int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr); std::string s(n > 0 ? n - 1 : 0, '\0'); if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr); return s; }

struct Info { std::string method, url; bool auth = false, signature = false; std::string contract; };
std::mutex g_lock;
std::map<IXMLHTTPRequest2*, Info> g_requests;
std::map<std::string, unsigned> g_counts;   // per method+path (no query): throttle repeated requests
bool Loud(const Info& i) {
    std::string key = i.method + " " + i.url.substr(0, i.url.find('?'));
    unsigned n = ++g_counts[key]; return n <= 3 || n % 500 == 0;
}

class Callback : public IXMLHTTPRequest2Callback {
    LONG refs_ = 1; ComPtr<IXMLHTTPRequest2Callback> inner_; Info info_;
public:
    Callback(IXMLHTTPRequest2Callback* inner, Info info) : inner_(inner), info_(std::move(info)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IXMLHTTPRequest2Callback)) { *out = static_cast<IXMLHTTPRequest2Callback*>(this); AddRef(); return S_OK; }
        *out = nullptr; return E_NOINTERFACE;   // no IXMLHTTPRequest3Callback: msxml falls back to the 2 interface
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { LONG r = InterlockedDecrement(&refs_); if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE OnRedirect(IXMLHTTPRequest2* x, const WCHAR* to) override { Trace("%s %s -> redirect %s", info_.method.c_str(), info_.url.c_str(), Narrow(to).c_str()); return inner_->OnRedirect(x, to); }
    HRESULT STDMETHODCALLTYPE OnHeadersAvailable(IXMLHTTPRequest2* x, DWORD status, const WCHAR* text) override {
        bool loud; { std::lock_guard<std::mutex> l(g_lock); loud = Loud(info_) || status >= 400; }
        if (loud) {
            std::string extra;
            for (const wchar_t* h : { L"x-err", L"WWW-Authenticate", L"x-xbl-err", L"X-343-Error", L"Content-Type" }) {
                WCHAR* v = nullptr; if (SUCCEEDED(x->GetResponseHeader(h, &v)) && v) { extra += " "; extra += Narrow(h); extra += "="; extra += Narrow(v).substr(0, 200); CoTaskMemFree(v); }
            }
            Trace("%s %s -> %lu %s [auth=%d sig=%d contract=%s]%s", info_.method.c_str(), info_.url.substr(0, 300).c_str(), status, Narrow(text).c_str(), info_.auth, info_.signature, info_.contract.c_str(), extra.c_str());
        }
        return inner_->OnHeadersAvailable(x, status, text);
    }
    HRESULT STDMETHODCALLTYPE OnDataAvailable(IXMLHTTPRequest2* x, ISequentialStream* s) override { return inner_->OnDataAvailable(x, s); }
    HRESULT STDMETHODCALLTYPE OnResponseReceived(IXMLHTTPRequest2* x, ISequentialStream* s) override {
        { std::lock_guard<std::mutex> l(g_lock); g_requests.erase(x); }
        return inner_->OnResponseReceived(x, s);
    }
    HRESULT STDMETHODCALLTYPE OnError(IXMLHTTPRequest2* x, HRESULT hr) override {
        Trace("%s %s -> transport error %08lx", info_.method.c_str(), info_.url.substr(0, 300).c_str(), hr);
        { std::lock_guard<std::mutex> l(g_lock); g_requests.erase(x); }
        return inner_->OnError(x, hr);
    }
};

using OpenFn = HRESULT(STDMETHODCALLTYPE*)(IXMLHTTPRequest2*, const WCHAR*, const WCHAR*, IXMLHTTPRequest2Callback*, const WCHAR*, const WCHAR*, const WCHAR*, const WCHAR*);
using HeaderFn = HRESULT(STDMETHODCALLTYPE*)(IXMLHTTPRequest2*, const WCHAR*, const WCHAR*);
OpenFn g_open = nullptr; HeaderFn g_header = nullptr;

HRESULT STDMETHODCALLTYPE Open(IXMLHTTPRequest2* x, const WCHAR* method, const WCHAR* url, IXMLHTTPRequest2Callback* cb, const WCHAR* u, const WCHAR* p, const WCHAR* pu, const WCHAR* pp) {
    Info info; info.method = Narrow(method); info.url = Narrow(url);
    // FORGE_LOBBY_FAILFAST=1: 343's lobby service refuses this copy (403) and the engine retries for over a minute
    // before falling back to its offline lobby. Fail those requests at once so the fallback happens at startup.
    static const bool failFast = GetEnvironmentVariableW(L"FORGE_LOBBY_FAILFAST", nullptr, 0) != 0;
    if (failFast && info.url.find("lobby.svc.halowaypoint.com") != std::string::npos) {
        Trace("%s %s -> refused locally (lobby fail-fast)", info.method.c_str(), info.url.substr(0, 200).c_str());
        return HRESULT_FROM_WIN32(12029);   // ERROR_INTERNET_CANNOT_CONNECT
    }
    { std::lock_guard<std::mutex> l(g_lock); g_requests[x] = info; }
    static volatile long lobbyStacks = 0;
    if (info.url.find("lobby.svc.") != std::string::npos && InterlockedIncrement(&lobbyStacks) <= 4) {
        void* frames[40]{}; USHORT got = RtlCaptureStackBackTrace(0, 40, frames, nullptr); std::string line = "lobby request stack:";
        for (USHORT i = 0; i < got; ++i) { HMODULE m = nullptr; char name[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(frames[i]), &m) && m) GetModuleFileNameA(m, name, MAX_PATH);
            const char* leaf = strrchr(name, '\\'); char b[96]; sprintf_s(b, " %s+%llx", leaf ? leaf + 1 : name, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(m))); line += b; }
        Trace("%s", line.c_str());
    }
    IXMLHTTPRequest2Callback* wrapped = cb ? new Callback(cb, info) : nullptr;
    HRESULT hr = g_open(x, method, url, wrapped ? wrapped : cb, u, p, pu, pp);
    if (wrapped) wrapped->Release();   // msxml holds its own reference
    if (FAILED(hr)) Trace("Open %s %s failed %08lx", info.method.c_str(), info.url.c_str(), hr);
    return hr;
}
HRESULT STDMETHODCALLTYPE SetHeader(IXMLHTTPRequest2* x, const WCHAR* name, const WCHAR* value) {
    if (name) {
        std::lock_guard<std::mutex> l(g_lock); auto it = g_requests.find(x);
        if (it != g_requests.end()) {
            if (!_wcsicmp(name, L"Authorization") || !_wcsicmp(name, L"X-343-Authorization-Spartan") || !_wcsicmp(name, L"X-343-Authorization-XSTS")) it->second.auth = true;
            else if (!_wcsicmp(name, L"Signature")) it->second.signature = true;
            else if (!_wcsicmp(name, L"x-xbl-contract-version")) it->second.contract = Narrow(value);
        }
    }
    return g_header(x, name, value);
}
// ---- WebSockets (ServiceLink / 343 lobby traffic) --------------------------------------------
using ABI::Windows::Networking::Sockets::IWebSocket;
using ABI::Windows::Foundation::IUriRuntimeClass;
using ABI::Windows::Foundation::IAsyncAction;
using ABI::Windows::Foundation::IAsyncInfo;
using WsConnectFn = HRESULT(STDMETHODCALLTYPE*)(IWebSocket*, IUriRuntimeClass*, IAsyncAction**);
using WsHeaderFn = HRESULT(STDMETHODCALLTYPE*)(IWebSocket*, HSTRING, HSTRING);
WsConnectFn g_wsConnect = nullptr; WsHeaderFn g_wsHeader = nullptr;
std::string HNarrow(HSTRING h) { UINT32 n = 0; auto p = WindowsGetStringRawBuffer(h, &n); return Narrow(std::wstring(p ? p : L"", n).c_str()); }
HRESULT STDMETHODCALLTYPE WsHeader(IWebSocket* w, HSTRING name, HSTRING value) {
    std::string n = HNarrow(name); bool secret = _stricmp(n.c_str(), "Authorization") == 0 || n.find("343-Authorization") != std::string::npos || _stricmp(n.c_str(), "Signature") == 0;
    Trace("websocket %p header %s%s%s", w, n.c_str(), secret ? "" : "=", secret ? " (present)" : HNarrow(value).substr(0, 120).c_str());
    return g_wsHeader(w, name, value);
}
HRESULT STDMETHODCALLTYPE WsConnect(IWebSocket* w, IUriRuntimeClass* uri, IAsyncAction** op) {
    Microsoft::WRL::Wrappers::HString raw; std::string url = uri && SUCCEEDED(uri->get_RawUri(raw.GetAddressOf())) ? HNarrow(raw.Get()) : "?";
    Trace("websocket %p connect %s", w, url.c_str());
    HRESULT hr = g_wsConnect(w, uri, op);
    if (FAILED(hr)) { Trace("websocket %p connect call failed %08lx", w, hr); return hr; }
    if (op && *op) {   // watch the operation without taking over its completion handler
        ComPtr<IAsyncInfo> info; (*op)->QueryInterface(IID_PPV_ARGS(&info));
        struct Watch { ComPtr<IAsyncInfo> info; std::string url; void* socket; };
        auto* watch = new Watch{ info, url, w };
        CreateThread(nullptr, 0, [](void* p) -> DWORD {
            std::unique_ptr<Watch> wch(static_cast<Watch*>(p));
            for (int i = 0; i < 600; ++i) {
                ABI::Windows::Foundation::AsyncStatus st; if (FAILED(wch->info->get_Status(&st))) return 0;
                if (st != ABI::Windows::Foundation::AsyncStatus::Started) {
                    HRESULT e = S_OK; wch->info->get_ErrorCode(&e);
                    Trace("websocket %p connect %s -> status %d hr=%08lx%s", wch->socket, wch->url.c_str(), static_cast<int>(st), e,
                          (e & 0xFFFF0000) == 0x80190000 ? " (HTTP status in low word)" : "");
                    return 0;
                }
                Sleep(100);
            }
            Trace("websocket %p connect %s still pending after 60 s", wch->socket, wch->url.c_str()); return 0;
        }, watch, 0, nullptr);
    }
    return hr;
}
void HookWebSocket() {
    ComPtr<IInspectable> obj;
    HRESULT hr = RoActivateInstance(Microsoft::WRL::Wrappers::HStringReference(L"Windows.Networking.Sockets.MessageWebSocket").Get(), &obj);
    ComPtr<IWebSocket> ws; if (SUCCEEDED(hr)) hr = obj.As(&ws);
    if (FAILED(hr)) { Trace("could not create MessageWebSocket (%08lx)", hr); return; }
    void** vtbl = *reinterpret_cast<void***>(ws.Get());
    // IWebSocket after IInspectable: get_OutputStream, ConnectAsync, SetRequestHeader, add_Closed, remove_Closed, CloseWithStatus
    bool ok = MH_CreateHook(vtbl[7], reinterpret_cast<void*>(&WsConnect), reinterpret_cast<void**>(&g_wsConnect)) == MH_OK && MH_EnableHook(vtbl[7]) == MH_OK;
    ok = ok && MH_CreateHook(vtbl[8], reinterpret_cast<void*>(&WsHeader), reinterpret_cast<void**>(&g_wsHeader)) == MH_OK && MH_EnableHook(vtbl[8]) == MH_OK;
    Trace(ok ? "tracing WebSocket connects" : "WebSocket hook install failed");
}

DWORD WINAPI Install(void*) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IXMLHTTPRequest2> probe;
    HRESULT hr = CoCreateInstance(CLSID_FreeThreadedXMLHTTP60, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&probe));
    if (FAILED(hr)) { Trace("could not create IXMLHTTPRequest2 (%08lx)", hr); return 0; }
    void** vtbl = *reinterpret_cast<void***>(probe.Get());
    // IXMLHTTPRequest2 after IUnknown: Open, Send, Abort, SetCookie, SetCustomResponseStream, SetProperty, SetRequestHeader, ...
    MH_STATUS st = MH_Initialize(); if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return 0;
    bool ok = MH_CreateHook(vtbl[3], reinterpret_cast<void*>(&Open), reinterpret_cast<void**>(&g_open)) == MH_OK && MH_EnableHook(vtbl[3]) == MH_OK;
    ok = ok && MH_CreateHook(vtbl[9], reinterpret_cast<void*>(&SetHeader), reinterpret_cast<void**>(&g_header)) == MH_OK && MH_EnableHook(vtbl[9]) == MH_OK;
    Trace(ok ? "tracing IXMLHTTPRequest2 traffic" : "hook install failed");
    ComPtr<IXMLHTTPRequest2> other;
    if (SUCCEEDED(CoCreateInstance(CLSID_XMLHTTP60, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&other)))) {
        void** v2 = *reinterpret_cast<void***>(other.Get());
        if (v2[3] != vtbl[3]) Trace("XMLHTTP60 has a separate implementation (not hooked)");
    }
    HookWebSocket();
    probe.Reset(); CoUninitialize(); return 0;
}
}

void InstallHttpTrace() {
    if (!GetEnvironmentVariableW(L"FORGE_TRACE_HTTP", nullptr, 0) && !GetEnvironmentVariableW(L"FORGE_LOBBY_FAILFAST", nullptr, 0)) return;
    HANDLE t = CreateThread(nullptr, 0, Install, nullptr, 0, nullptr);
    if (t) { WaitForSingleObject(t, 5000); CloseHandle(t); }
}
