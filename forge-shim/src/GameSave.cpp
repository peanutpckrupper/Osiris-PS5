// Windows.Gaming.XboxLive.Storage replacement: game saves ("connected storage") kept as local files.
// The real GameSaveProvider needs a Store package and Windows' Xbox Live sign-in, so unpackaged Forge
// gets "saving failed". Layout: %LOCALAPPDATA%\ForgeStandalone\GameSaves\<scid>\<container>\<blob>.blob
// (names percent-encoded), plus <container>\displayname.txt. Every operation completes synchronously.
#include <windows.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <roapi.h>
#include <robuffer.h>
#include <windows.gaming.xboxlive.storage.h>
#include <windows.storage.streams.h>
#include <windows.foundation.collections.h>
#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <typeinfo>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::Gaming::XboxLive::Storage;
using ABI::Windows::Storage::Streams::IBuffer;
using ABI::Windows::Storage::Streams::IBufferFactory;
using ABI::Windows::System::IUser;

namespace {
void Trace(const char* fmt, ...) { char b[700]; va_list a; va_start(a, fmt); _vsnprintf_s(b, _TRUNCATE, fmt, a); va_end(a); OutputDebugStringA("[ForgeShim] GameSave "); OutputDebugStringA(b); OutputDebugStringA("\n"); }
std::wstring FromH(HSTRING h) { UINT32 n = 0; auto p = WindowsGetStringRawBuffer(h, &n); return std::wstring(p ? p : L"", n); }
HRESULT Str(const std::wstring& s, HSTRING* out) { if (!out) return E_POINTER; return WindowsCreateString(s.c_str(), static_cast<UINT32>(s.size()), out); }
#define GS_NAME(n) public: HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* v) override { return WindowsCreateString(n, static_cast<UINT32>(wcslen(n)), v); } \
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* t) override { *t = BaseTrust; return S_OK; } private:
#define GS_QI(...) public: HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override { \
    HRESULT hr = RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, __VA_ARGS__>::QueryInterface(iid, out); \
    if (FAILED(hr)) { wchar_t g[64]; StringFromGUID2(iid, g, 64); Trace("QI miss on %s for %ls", typeid(*this).name(), g); } return hr; } private:

// ---- files ---------------------------------------------------------------------------------
std::wstring Encode(const std::wstring& name) {
    std::wstring out; wchar_t hex[8];
    for (wchar_t c : name) {
        bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'_' || c == L'.' || c == L' ';
        if (ok) out += c; else { swprintf_s(hex, L"%%%04X", static_cast<unsigned>(c)); out += hex; }
    }
    if (out.empty() || out == L"." || out == L"..") out = L"%0000" + out;
    return out;
}
std::wstring Decode(const std::wstring& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'%' && i + 4 < s.size()) { out += static_cast<wchar_t>(wcstoul(s.substr(i + 1, 4).c_str(), nullptr, 16)); i += 4; }
        else out += s[i];
    }
    return out;
}
std::wstring Root(const std::wstring& scid) {
    // FORGE_SAVE_SET=<name>: a separate save store, ForgeStandalone\GameSaves-<name> (automated test runs use one so
    // they never touch the player's saves).
    wchar_t b[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", b, MAX_PATH);
    wchar_t set[64]{}; DWORD sn = GetEnvironmentVariableW(L"FORGE_SAVE_SET", set, 64);
    std::wstring base = (n && n < MAX_PATH) ? std::wstring(b) : std::wstring(L".");
    std::wstring path = base + L"\\ForgeStandalone"; CreateDirectoryW(path.c_str(), nullptr);
    path += L"\\GameSaves"; if (sn && sn < 64) path += std::wstring(L"-") + set; CreateDirectoryW(path.c_str(), nullptr);
    path += L"\\" + Encode(scid.empty() ? L"default" : scid); CreateDirectoryW(path.c_str(), nullptr);
    return path;
}
bool ReadFileAll(const std::wstring& path, std::vector<BYTE>& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{}; GetFileSizeEx(f, &size); data.resize(static_cast<size_t>(size.QuadPart)); DWORD got = 0;
    BOOL ok = data.empty() || ReadFile(f, data.data(), static_cast<DWORD>(data.size()), &got, nullptr); CloseHandle(f);
    return ok && got == data.size();
}
bool WriteFileAll(const std::wstring& path, const BYTE* data, size_t size) {
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0; BOOL ok = size == 0 || WriteFile(f, data, static_cast<DWORD>(size), &put, nullptr); FlushFileBuffers(f); CloseHandle(f);
    return ok && put == size && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
std::vector<std::wstring> List(const std::wstring& dir, bool directories) {
    std::vector<std::wstring> out; WIN32_FIND_DATAW d; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &d);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring n = d.cFileName; if (n == L"." || n == L"..") continue;
        bool isDir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (directories && isDir) out.push_back(n);
        if (!directories && !isDir && n.size() > 5 && n.compare(n.size() - 5, 5, L".blob") == 0) out.push_back(n.substr(0, n.size() - 5));
    } while (FindNextFileW(h, &d)); FindClose(h);
    return out;
}
void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW d; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &d);
    if (h != INVALID_HANDLE_VALUE) { do { std::wstring n = d.cFileName; if (n == L"." || n == L"..") continue; std::wstring p = dir + L"\\" + n;
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(p); else DeleteFileW(p.c_str()); } while (FindNextFileW(h, &d)); FindClose(h); }
    RemoveDirectoryW(dir.c_str());
}

// ---- buffers -------------------------------------------------------------------------------
BYTE* Bytes(IBuffer* b) { ComPtr<Windows::Storage::Streams::IBufferByteAccess> a; BYTE* p = nullptr; if (b && SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&a)))) a->Buffer(&p); return p; }
ComPtr<IBuffer> NewBuffer(const std::vector<BYTE>& data) {
    static ComPtr<IBufferFactory> factory;
    if (!factory) RoGetActivationFactory(HStringReference(L"Windows.Storage.Streams.Buffer").Get(), IID_PPV_ARGS(&factory));
    ComPtr<IBuffer> b; if (!factory || FAILED(factory->Create(static_cast<UINT32>(data.size() ? data.size() : 1), &b))) return nullptr;
    if (!data.empty()) memcpy(Bytes(b.Get()), data.data(), data.size());
    b->put_Length(static_cast<UINT32>(data.size())); return b;
}

// ---- completed async operations ------------------------------------------------------------
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
template <typename T>
class DoneValue : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IAsyncOperation<T>, IAsyncInfo> {
    T value_; ComPtr<IAsyncOperationCompletedHandler<T>> handler_;
public:
    explicit DoneValue(T v) : value_(v) {}
    HRESULT STDMETHODCALLTYPE put_Completed(IAsyncOperationCompletedHandler<T>* h) override { handler_ = h; return h ? h->Invoke(this, AsyncStatus::Completed) : S_OK; }
    HRESULT STDMETHODCALLTYPE get_Completed(IAsyncOperationCompletedHandler<T>** h) override { return handler_.CopyTo(h); }
    HRESULT STDMETHODCALLTYPE GetResults(T* r) override { if (!r) return E_POINTER; *r = value_; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Id(unsigned* id) override { *id = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Status(AsyncStatus* s) override { *s = AsyncStatus::Completed; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* e) override { *e = S_OK; return S_OK; }
    HRESULT STDMETHODCALLTYPE Cancel() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
template <typename TRuntime, typename TAbi, typename TOut>
HRESULT Complete(TAbi* value, TOut** out) { if (!out) return E_POINTER; auto op = Make<Done<TRuntime, TAbi>>(value); return op ? op.CopyTo(out) : E_OUTOFMEMORY; }

// ---- collections ---------------------------------------------------------------------------
template <typename TRuntime, typename TAbi>
class View : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IVectorView<TRuntime>> {
    std::vector<ComPtr<TAbi>> items_;
public:
    explicit View(std::vector<ComPtr<TAbi>> items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE GetAt(unsigned i, TAbi** v) override { if (i >= items_.size()) return E_BOUNDS; return items_[i].CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* n) override { *n = static_cast<unsigned>(items_.size()); return S_OK; }
    HRESULT STDMETHODCALLTYPE IndexOf(TAbi* x, unsigned* i, boolean* f) override { *f = false; *i = 0; for (unsigned k = 0; k < items_.size(); ++k) if (items_[k].Get() == x) { *i = k; *f = true; } return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMany(unsigned start, unsigned cap, TAbi** out, unsigned* got) override {
        *got = 0; for (unsigned k = start; k < items_.size() && *got < cap; ++k) items_[k].CopyTo(&out[(*got)++]); return S_OK; }
};
class StringView : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IVectorView<HSTRING>> {
public:
    HRESULT STDMETHODCALLTYPE GetAt(unsigned, HSTRING*) override { return E_BOUNDS; }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* n) override { *n = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE IndexOf(HSTRING, unsigned* i, boolean* f) override { *i = 0; *f = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMany(unsigned, unsigned, HSTRING*, unsigned* got) override { *got = 0; return S_OK; }
};
using BlobPair = IKeyValuePair<HSTRING, IBuffer*>;
class Pair : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, BlobPair> {
    std::wstring key_; ComPtr<IBuffer> value_;
public:
    Pair(std::wstring k, IBuffer* v) : key_(std::move(k)), value_(v) {}
    HRESULT STDMETHODCALLTYPE get_Key(HSTRING* k) override { return Str(key_, k); }
    HRESULT STDMETHODCALLTYPE get_Value(IBuffer** v) override { return value_.CopyTo(v); }
};
class PairIterator : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IIterator<BlobPair*>> {
    std::vector<std::pair<std::wstring, ComPtr<IBuffer>>> items_; unsigned at_ = 0;
public:
    explicit PairIterator(std::vector<std::pair<std::wstring, ComPtr<IBuffer>>> items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE get_Current(BlobPair** v) override { if (at_ >= items_.size()) return E_BOUNDS; auto p = Make<Pair>(items_[at_].first, items_[at_].second.Get()); return p.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* v) override { *v = at_ < items_.size(); return S_OK; }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* v) override { if (at_ < items_.size()) ++at_; *v = at_ < items_.size(); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMany(unsigned cap, BlobPair** out, unsigned* got) override {
        *got = 0; while (at_ < items_.size() && *got < cap) { auto p = Make<Pair>(items_[at_].first, items_[at_].second.Get()); p.CopyTo(&out[(*got)++]); ++at_; } return S_OK; }
};
class BlobMap : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IMapView<HSTRING, IBuffer*>, IIterable<BlobPair*>> {
    GS_QI(IMapView<HSTRING, IBuffer*>, IIterable<BlobPair*>)
    GS_NAME(L"ForgeShim.GameSaveBlobMap")
    std::map<std::wstring, ComPtr<IBuffer>> items_;
public:
    explicit BlobMap(std::map<std::wstring, ComPtr<IBuffer>> items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE Lookup(HSTRING key, IBuffer** v) override { auto it = items_.find(FromH(key)); if (it == items_.end()) { *v = nullptr; return E_BOUNDS; } return it->second.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* n) override { *n = static_cast<unsigned>(items_.size()); return S_OK; }
    HRESULT STDMETHODCALLTYPE HasKey(HSTRING key, boolean* f) override { *f = items_.count(FromH(key)) != 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Split(IMapView<HSTRING, IBuffer*>** a, IMapView<HSTRING, IBuffer*>** b) override { *a = nullptr; *b = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE First(IIterator<BlobPair*>** v) override {
        std::vector<std::pair<std::wstring, ComPtr<IBuffer>>> list(items_.begin(), items_.end()); auto it = Make<PairIterator>(std::move(list)); return it.CopyTo(v); }
};
// Reads any IMapView<String, IBuffer> the game passes in (through its IIterable side).
bool Entries(IMapView<HSTRING, IBuffer*>* map, std::vector<std::pair<std::wstring, ComPtr<IBuffer>>>& out) {
    if (!map) return true;
    ComPtr<IIterable<BlobPair*>> iterable; if (FAILED(map->QueryInterface(IID_PPV_ARGS(&iterable)))) { Trace("blob map has no iterator"); return false; }
    ComPtr<IIterator<BlobPair*>> it; if (FAILED(iterable->First(&it))) return false;
    boolean has = false; it->get_HasCurrent(&has);
    while (has) { ComPtr<BlobPair> p; it->get_Current(&p); HString k; ComPtr<IBuffer> v; p->get_Key(k.GetAddressOf()); p->get_Value(&v); out.push_back({ FromH(k.Get()), v }); it->MoveNext(&has); }
    return true;
}
std::vector<std::wstring> Names(IIterable<HSTRING>* names) {
    std::vector<std::wstring> out; if (!names) return out;
    ComPtr<IIterator<HSTRING>> it; if (FAILED(names->First(&it))) return out;
    boolean has = false; it->get_HasCurrent(&has);
    while (has) { HString s; it->get_Current(s.GetAddressOf()); out.push_back(FromH(s.Get())); it->MoveNext(&has); }
    return out;
}

// ---- results -------------------------------------------------------------------------------
class OperationResult : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IGameSaveOperationResult> {
    GS_QI(IGameSaveOperationResult) GS_NAME(L"Windows.Gaming.XboxLive.Storage.GameSaveOperationResult")
    GameSaveErrorStatus status_;
public:
    explicit OperationResult(GameSaveErrorStatus s) : status_(s) {}
    HRESULT STDMETHODCALLTYPE get_Status(GameSaveErrorStatus* v) override { *v = status_; return S_OK; }
};
HRESULT Operation(GameSaveErrorStatus s, IAsyncOperation<GameSaveOperationResult*>** out) { auto r = Make<OperationResult>(s); return Complete<GameSaveOperationResult*, IGameSaveOperationResult>(r.Get(), out); }

class BlobGetResult : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IGameSaveBlobGetResult> {
    GS_QI(IGameSaveBlobGetResult) GS_NAME(L"Windows.Gaming.XboxLive.Storage.GameSaveBlobGetResult")
    GameSaveErrorStatus status_; ComPtr<IMapView<HSTRING, IBuffer*>> value_;
public:
    BlobGetResult(GameSaveErrorStatus s, IMapView<HSTRING, IBuffer*>* v) : status_(s), value_(v) {}
    HRESULT STDMETHODCALLTYPE get_Status(GameSaveErrorStatus* v) override { *v = status_; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Value(IMapView<HSTRING, IBuffer*>** v) override { return value_.CopyTo(v); }
};

class BlobInfo : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IGameSaveBlobInfo> {
    GS_QI(IGameSaveBlobInfo) GS_NAME(L"Windows.Gaming.XboxLive.Storage.GameSaveBlobInfo")
    std::wstring name_; UINT32 size_;
public:
    BlobInfo(std::wstring n, UINT32 s) : name_(std::move(n)), size_(s) {}
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* v) override { return Str(name_, v); }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* v) override { *v = size_; return S_OK; }
};
class BlobInfoGetResult : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IGameSaveBlobInfoGetResult> {
    GS_QI(IGameSaveBlobInfoGetResult) GS_NAME(L"Windows.Gaming.XboxLive.Storage.GameSaveBlobInfoGetResult")
    ComPtr<IVectorView<GameSaveBlobInfo*>> value_;
public:
    explicit BlobInfoGetResult(IVectorView<GameSaveBlobInfo*>* v) : value_(v) {}
    HRESULT STDMETHODCALLTYPE get_Status(GameSaveErrorStatus* v) override { *v = GameSaveErrorStatus_Ok; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Value(IVectorView<GameSaveBlobInfo*>** v) override { return value_.CopyTo(v); }
};
class BlobInfoQuery : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IGameSaveBlobInfoQuery> {
    GS_QI(IGameSaveBlobInfoQuery) GS_NAME(L"Windows.Gaming.XboxLive.Storage.GameSaveBlobInfoQuery")
    std::wstring dir_, prefix_;
    std::vector<ComPtr<IGameSaveBlobInfo>> Items(UINT32 start, UINT32 max) {
        std::vector<ComPtr<IGameSaveBlobInfo>> all;
        for (auto& file : List(dir_, false)) {
            std::wstring name = Decode(file); if (name.compare(0, prefix_.size(), prefix_) != 0) continue;
            WIN32_FILE_ATTRIBUTE_DATA a{}; GetFileAttributesExW((dir_ + L"\\" + file + L".blob").c_str(), GetFileExInfoStandard, &a);
            all.push_back(Make<BlobInfo>(name, a.nFileSizeLow));
        }
        std::vector<ComPtr<IGameSaveBlobInfo>> out; for (UINT32 i = start; i < all.size() && out.size() < max; ++i) out.push_back(all[i]); return out;
    }
public:
    BlobInfoQuery(std::wstring dir, std::wstring prefix) : dir_(std::move(dir)), prefix_(std::move(prefix)) {}
    HRESULT STDMETHODCALLTYPE GetBlobInfoAsync(IAsyncOperation<GameSaveBlobInfoGetResult*>** v) override { return GetBlobInfoWithIndexAndMaxAsync(0, 0xFFFFFFFF, v); }
    HRESULT STDMETHODCALLTYPE GetBlobInfoWithIndexAndMaxAsync(UINT32 start, UINT32 max, IAsyncOperation<GameSaveBlobInfoGetResult*>** v) override {
        auto view = Make<View<GameSaveBlobInfo*, IGameSaveBlobInfo>>(Items(start, max)); auto r = Make<BlobInfoGetResult>(view.Get());
        return Complete<GameSaveBlobInfoGetResult*, IGameSaveBlobInfoGetResult>(r.Get(), v); }
    HRESULT STDMETHODCALLTYPE GetItemCountAsync(IAsyncOperation<UINT32>** v) override { auto op = Make<DoneValue<UINT32>>(static_cast<UINT32>(Items(0, 0xFFFFFFFF).size())); return op.CopyTo(v); }
};

class ContainerInfo : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IGameSaveContainerInfo> {
    GS_QI(IGameSaveContainerInfo) GS_NAME(L"Windows.Gaming.XboxLive.Storage.GameSaveContainerInfo")
    std::wstring name_, display_; UINT64 size_; DateTime time_;
public:
    ContainerInfo(std::wstring n, std::wstring d, UINT64 s, DateTime t) : name_(std::move(n)), display_(std::move(d)), size_(s), time_(t) {}
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* v) override { return Str(name_, v); }
    HRESULT STDMETHODCALLTYPE get_TotalSize(UINT64* v) override { *v = size_; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_DisplayName(HSTRING* v) override { return Str(display_, v); }
    HRESULT STDMETHODCALLTYPE get_LastModifiedTime(DateTime* v) override { *v = time_; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_NeedsSync(boolean* v) override { *v = false; return S_OK; }
};
class ContainerInfoGetResult : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IGameSaveContainerInfoGetResult> {
    GS_QI(IGameSaveContainerInfoGetResult) GS_NAME(L"Windows.Gaming.XboxLive.Storage.GameSaveContainerInfoGetResult")
    ComPtr<IVectorView<GameSaveContainerInfo*>> value_;
public:
    explicit ContainerInfoGetResult(IVectorView<GameSaveContainerInfo*>* v) : value_(v) {}
    HRESULT STDMETHODCALLTYPE get_Status(GameSaveErrorStatus* v) override { *v = GameSaveErrorStatus_Ok; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Value(IVectorView<GameSaveContainerInfo*>** v) override { return value_.CopyTo(v); }
};
class ContainerInfoQuery : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IGameSaveContainerInfoQuery> {
    GS_QI(IGameSaveContainerInfoQuery) GS_NAME(L"Windows.Gaming.XboxLive.Storage.GameSaveContainerInfoQuery")
    std::wstring root_, prefix_;
    std::vector<ComPtr<IGameSaveContainerInfo>> Items(UINT32 start, UINT32 max) {
        std::vector<ComPtr<IGameSaveContainerInfo>> all;
        for (auto& dir : List(root_, true)) {
            std::wstring name = Decode(dir); if (name.compare(0, prefix_.size(), prefix_) != 0) continue;
            std::wstring path = root_ + L"\\" + dir; UINT64 total = 0; FILETIME latest{};
            for (auto& blob : List(path, false)) { WIN32_FILE_ATTRIBUTE_DATA a{}; if (GetFileAttributesExW((path + L"\\" + blob + L".blob").c_str(), GetFileExInfoStandard, &a)) {
                total += a.nFileSizeLow; if (CompareFileTime(&a.ftLastWriteTime, &latest) > 0) latest = a.ftLastWriteTime; } }
            std::vector<BYTE> d; std::wstring display = name;
            if (ReadFileAll(path + L"\\displayname.txt", d) && !d.empty()) display.assign(reinterpret_cast<const wchar_t*>(d.data()), d.size() / sizeof(wchar_t));
            DateTime t{}; t.UniversalTime = (static_cast<INT64>(latest.dwHighDateTime) << 32) | latest.dwLowDateTime;
            all.push_back(Make<ContainerInfo>(name, display, total, t));
        }
        std::vector<ComPtr<IGameSaveContainerInfo>> out; for (UINT32 i = start; i < all.size() && out.size() < max; ++i) out.push_back(all[i]); return out;
    }
public:
    ContainerInfoQuery(std::wstring root, std::wstring prefix) : root_(std::move(root)), prefix_(std::move(prefix)) {}
    HRESULT STDMETHODCALLTYPE GetContainerInfoAsync(IAsyncOperation<GameSaveContainerInfoGetResult*>** v) override { return GetContainerInfoWithIndexAndMaxAsync(0, 0xFFFFFFFF, v); }
    HRESULT STDMETHODCALLTYPE GetContainerInfoWithIndexAndMaxAsync(UINT32 start, UINT32 max, IAsyncOperation<GameSaveContainerInfoGetResult*>** v) override {
        auto items = Items(start, max); Trace("container query '%ls': %zu containers", prefix_.c_str(), items.size());
        auto view = Make<View<GameSaveContainerInfo*, IGameSaveContainerInfo>>(std::move(items)); auto r = Make<ContainerInfoGetResult>(view.Get());
        return Complete<GameSaveContainerInfoGetResult*, IGameSaveContainerInfoGetResult>(r.Get(), v); }
    HRESULT STDMETHODCALLTYPE GetItemCountAsync(IAsyncOperation<UINT32>** v) override { auto op = Make<DoneValue<UINT32>>(static_cast<UINT32>(Items(0, 0xFFFFFFFF).size())); return op.CopyTo(v); }
};

// ---- container / provider ------------------------------------------------------------------
class Provider;
class Container : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IGameSaveContainer> {
    GS_QI(IGameSaveContainer) GS_NAME(L"Windows.Gaming.XboxLive.Storage.GameSaveContainer")
    ComPtr<IGameSaveProvider> provider_; std::wstring name_, dir_;
    std::wstring Blob(const std::wstring& n) { return dir_ + L"\\" + Encode(n) + L".blob"; }
    HRESULT Write(const std::vector<std::pair<std::wstring, ComPtr<IBuffer>>>& writes, const std::vector<std::wstring>& deletes, const std::wstring& display, IAsyncOperation<GameSaveOperationResult*>** out) {
        CreateDirectoryW(dir_.c_str(), nullptr); size_t bytes = 0;
        for (auto& w : writes) {
            UINT32 len = 0; if (w.second) w.second->get_Length(&len);
            if (!WriteFileAll(Blob(w.first), w.second ? Bytes(w.second.Get()) : nullptr, len)) { Trace("write failed: %ls/%ls (err %lu)", name_.c_str(), w.first.c_str(), GetLastError()); return Operation(GameSaveErrorStatus_Abort, out); }
            bytes += len;
        }
        for (auto& d : deletes) DeleteFileW(Blob(d).c_str());
        if (!display.empty()) WriteFileAll(dir_ + L"\\displayname.txt", reinterpret_cast<const BYTE*>(display.data()), display.size() * sizeof(wchar_t));
        Trace("saved %ls: %zu blobs (%zu bytes), %zu deleted", name_.c_str(), writes.size(), bytes, deletes.size());
        return Operation(GameSaveErrorStatus_Ok, out);
    }
public:
    Container(IGameSaveProvider* p, std::wstring name, std::wstring root) : provider_(p), name_(std::move(name)) { dir_ = root + L"\\" + Encode(name_); }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* v) override { return Str(name_, v); }
    HRESULT STDMETHODCALLTYPE get_Provider(IGameSaveProvider** v) override { return provider_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE SubmitUpdatesAsync(IMapView<HSTRING, IBuffer*>* write, IIterable<HSTRING>* remove, HSTRING display, IAsyncOperation<GameSaveOperationResult*>** out) override {
        std::vector<std::pair<std::wstring, ComPtr<IBuffer>>> writes; if (!Entries(write, writes)) return Operation(GameSaveErrorStatus_Abort, out);
        return Write(writes, Names(remove), FromH(display), out);
    }
    HRESULT STDMETHODCALLTYPE ReadAsync(IMapView<HSTRING, IBuffer*>* read, IAsyncOperation<GameSaveOperationResult*>** out) override {
        std::vector<std::pair<std::wstring, ComPtr<IBuffer>>> wanted; if (!Entries(read, wanted)) return Operation(GameSaveErrorStatus_Abort, out);
        for (auto& w : wanted) {
            std::vector<BYTE> d; if (!ReadFileAll(Blob(w.first), d)) { Trace("read %ls/%ls: missing", name_.c_str(), w.first.c_str()); return Operation(GameSaveErrorStatus_BlobNotFound, out); }
            UINT32 cap = 0; w.second->get_Capacity(&cap); if (d.size() > cap) return Operation(GameSaveErrorStatus_ProvidedBufferTooSmall, out);
            if (!d.empty()) memcpy(Bytes(w.second.Get()), d.data(), d.size()); w.second->put_Length(static_cast<UINT32>(d.size()));
        }
        Trace("read %ls: %zu blobs into caller buffers", name_.c_str(), wanted.size());
        return Operation(GameSaveErrorStatus_Ok, out);
    }
    HRESULT STDMETHODCALLTYPE GetAsync(IIterable<HSTRING>* names, IAsyncOperation<GameSaveBlobGetResult*>** out) override {
        std::map<std::wstring, ComPtr<IBuffer>> found; GameSaveErrorStatus status = GameSaveErrorStatus_Ok;
        for (auto& n : Names(names)) { std::vector<BYTE> d; if (ReadFileAll(Blob(n), d)) found[n] = NewBuffer(d); else status = GameSaveErrorStatus_BlobNotFound; }
        Trace("get %ls: %zu blobs (status %d)", name_.c_str(), found.size(), static_cast<int>(status));
        auto map = Make<BlobMap>(std::move(found)); auto r = Make<BlobGetResult>(status, map.Get());
        return Complete<GameSaveBlobGetResult*, IGameSaveBlobGetResult>(r.Get(), out);
    }
    HRESULT STDMETHODCALLTYPE SubmitPropertySetUpdatesAsync(IPropertySet* set, IIterable<HSTRING>* remove, HSTRING display, IAsyncOperation<GameSaveOperationResult*>** out) override {
        std::vector<std::pair<std::wstring, ComPtr<IBuffer>>> writes;
        ComPtr<IIterable<IKeyValuePair<HSTRING, IInspectable*>*>> iterable;
        if (set && SUCCEEDED(set->QueryInterface(IID_PPV_ARGS(&iterable)))) {
            ComPtr<IIterator<IKeyValuePair<HSTRING, IInspectable*>*>> it; iterable->First(&it); boolean has = false; if (it) it->get_HasCurrent(&has);
            while (has) { ComPtr<IKeyValuePair<HSTRING, IInspectable*>> p; it->get_Current(&p); HString k; ComPtr<IInspectable> v; p->get_Key(k.GetAddressOf()); p->get_Value(&v);
                ComPtr<IBuffer> b; if (v) v.As(&b); writes.push_back({ FromH(k.Get()), b }); it->MoveNext(&has); }
        }
        return Write(writes, Names(remove), FromH(display), out);
    }
    HRESULT STDMETHODCALLTYPE CreateBlobInfoQuery(HSTRING prefix, IGameSaveBlobInfoQuery** v) override { auto q = Make<BlobInfoQuery>(dir_, FromH(prefix)); return q.CopyTo(v); }
};

class Provider : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IGameSaveProvider> {
    GS_QI(IGameSaveProvider) GS_NAME(L"Windows.Gaming.XboxLive.Storage.GameSaveProvider")
    ComPtr<IUser> user_; std::wstring root_;
public:
    Provider(IUser* u, std::wstring root) : user_(u), root_(std::move(root)) {}
    HRESULT STDMETHODCALLTYPE get_User(IUser** v) override { return user_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE CreateContainer(HSTRING name, IGameSaveContainer** v) override {
        Trace("container %ls", FromH(name).c_str()); auto c = Make<Container>(this, FromH(name), root_); return c.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE DeleteContainerAsync(HSTRING name, IAsyncOperation<GameSaveOperationResult*>** v) override {
        std::wstring dir = root_ + L"\\" + Encode(FromH(name));
        RemoveTree(dir); Trace("deleted container %ls", FromH(name).c_str()); return Operation(GameSaveErrorStatus_Ok, v); }
    HRESULT STDMETHODCALLTYPE CreateContainerInfoQuery(IGameSaveContainerInfoQuery** v) override { auto q = Make<ContainerInfoQuery>(root_, L""); return q.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE CreateContainerInfoQueryWithName(HSTRING prefix, IGameSaveContainerInfoQuery** v) override { auto q = Make<ContainerInfoQuery>(root_, FromH(prefix)); return q.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE GetRemainingBytesInQuotaAsync(IAsyncOperation<INT64>** v) override { auto op = Make<DoneValue<INT64>>(256LL * 1024 * 1024); return op.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_ContainersChangedSinceLastSync(IVectorView<HSTRING>** v) override { auto e = Make<StringView>(); return e.CopyTo(v); }
};

class GetResult : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IGameSaveProviderGetResult> {
    GS_QI(IGameSaveProviderGetResult) GS_NAME(L"Windows.Gaming.XboxLive.Storage.GameSaveProviderGetResult")
    ComPtr<IGameSaveProvider> value_;
public:
    explicit GetResult(IGameSaveProvider* p) : value_(p) {}
    HRESULT STDMETHODCALLTYPE get_Status(GameSaveErrorStatus* v) override { *v = GameSaveErrorStatus_Ok; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Value(IGameSaveProvider** v) override { return value_.CopyTo(v); }
};

class Statics : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, IGameSaveProviderStatics> {
    GS_QI(IActivationFactory, IGameSaveProviderStatics)
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** v) override { if (v) *v = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetForUserAsync(IUser* user, HSTRING scid, IAsyncOperation<GameSaveProviderGetResult*>** v) override {
        std::wstring root = Root(FromH(scid)); Trace("provider for scid %ls at %ls", FromH(scid).c_str(), root.c_str());
        auto p = Make<Provider>(user, root); auto r = Make<GetResult>(p.Get());
        return Complete<GameSaveProviderGetResult*, IGameSaveProviderGetResult>(r.Get(), v); }
    HRESULT STDMETHODCALLTYPE GetSyncOnDemandForUserAsync(IUser* user, HSTRING scid, IAsyncOperation<GameSaveProviderGetResult*>** v) override { return GetForUserAsync(user, scid, v); }
};
}

HRESULT GameSaveProviderActivation(REFIID iid, void** out) { static ComPtr<Statics> s = Make<Statics>(); return s ? s.CopyTo(iid, out) : E_OUTOFMEMORY; }
