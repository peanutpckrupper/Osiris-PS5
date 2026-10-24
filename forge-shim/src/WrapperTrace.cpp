// Diagnostic: log calls into Halo's Xbox Live wrapper and ServiceLink exports (args + return).
// Enabled with FORGE_TRACE_WRAPPER=1. Only hooks exports with <= 4 integer/pointer arguments.
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <utility>
#include <array>
#include "../third_party/minhook/include/MinHook.h"

namespace {
constexpr int kMaxSlots = 160;
struct Slot { char name[120]; void* original; std::atomic<int> calls; };
Slot g_slots[kMaxSlots];
int g_used = 0;

void Emit(const char* fmt, ...) { char b[512]; va_list a; va_start(a, fmt); vsprintf_s(b, fmt, a); va_end(a); OutputDebugStringA(b); }

using Fn = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t);
template <int I> uint64_t __fastcall Thunk(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    Slot& s = g_slots[I];
    int n = ++s.calls; bool show = n <= 6 || (n & (n - 1)) == 0;
    if (show) Emit("[ForgeShim] XBLW %llu enter %s #%d args=%llx %llx %llx %llx tid=%lu\n", GetTickCount64() % 1000000, s.name, n, a, b, c, d, GetCurrentThreadId());
    uint64_t r = reinterpret_cast<Fn>(s.original)(a, b, c, d);
    if (show) Emit("[ForgeShim] XBLW %llu leave %s #%d ret=%llx (al=%u)\n", GetTickCount64() % 1000000, s.name, n, r, static_cast<unsigned>(r & 0xff));
    return r;
}
template <std::size_t... I> constexpr auto MakeTable(std::index_sequence<I...>) { return std::array<void*, sizeof...(I)>{ reinterpret_cast<void*>(&Thunk<I>)... }; }
}
static const auto g_thunks = MakeTable(std::make_index_sequence<kMaxSlots>{});

static const char* kPatterns[] = {
    "UserManager_", "@License@", "HasXboxLIVEConnection", "hasInternetConnection", "IsLocalUser@", "LiveConnectivityManager_",
    "CheckLiveConnectivity", "?Initialize@XboxLiveServicesEngineInterface", "ValidateUserOnResume", "HandleUserSignoutConfirmation",
    "ProfileManager_GetProfile@", "ConnectedStorageManager_IsReadyForXuid", "TMS_UserCompletionEventTriggered", "TMS_GetServiceConfig",
    "ForceRecheck", "?OnResume@", "?IsOnline@ServiceLink", "?GoOffline@ServiceLink", "?GoOnline@ServiceLink",
    "SetUserSigninInterface", "?Get@SpartanTokenManager",
};

static void HookModule(const wchar_t* moduleName) {
    HMODULE m = GetModuleHandleW(moduleName); if (!m) { Emit("[ForgeShim] XBLW module %ls not loaded\n", moduleName); return; }
    auto base = reinterpret_cast<BYTE*>(m); auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]; if (!dir.VirtualAddress) return;
    auto exp = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    auto names = reinterpret_cast<DWORD*>(base + exp->AddressOfNames); auto ords = reinterpret_cast<WORD*>(base + exp->AddressOfNameOrdinals); auto funcs = reinterpret_cast<DWORD*>(base + exp->AddressOfFunctions);
    for (DWORD i = 0; i < exp->NumberOfNames && g_used < kMaxSlots; ++i) {
        const char* n = reinterpret_cast<const char*>(base + names[i]);
        bool want = false; for (auto* p : kPatterns) if (strstr(n, p)) { want = true; break; }
        if (!want || strstr(n, "SaveBuffer") || strstr(n, "LoadBuffer")) continue;
        void* target = base + funcs[ords[i]];
        Slot& s = g_slots[g_used]; strncpy_s(s.name, n, _TRUNCATE);
        if (MH_CreateHook(target, g_thunks[g_used], &s.original) == MH_OK && MH_EnableHook(target) == MH_OK) { Emit("[ForgeShim] XBLW hooked %s\n", n); ++g_used; }
    }
}

static void HookVtable(const wchar_t* moduleName, const char* exportName, const char* label) {
    HMODULE m = GetModuleHandleW(moduleName); if (!m) return;
    // exportName is a static Get() returning the singleton; hook the live object's real vtable.
    using GetFn = void* (*)(); auto get = reinterpret_cast<GetFn>(GetProcAddress(m, exportName)); if (!get) { Emit("[ForgeShim] XBLW no getter %s\n", label); return; }
    void* obj = get(); if (!obj) { Emit("[ForgeShim] XBLW null singleton %s\n", label); return; }
    auto vt = *reinterpret_cast<void***>(obj);
    Emit("[ForgeShim] XBLW %s object=%p vtable=+%llx\n", label, obj, static_cast<unsigned long long>(reinterpret_cast<BYTE*>(vt) - reinterpret_cast<BYTE*>(m)));
    auto base = reinterpret_cast<BYTE*>(m); auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    BYTE* lo = base; BYTE* hi = base + nt->OptionalHeader.SizeOfImage;
    for (int i = 0; i < 80 && g_used < kMaxSlots; ++i) {
        auto f = reinterpret_cast<BYTE*>(vt[i]); if (f < lo || f >= hi) break;
        if (i == 0) continue;   // destructor
        if (i > 0 && vt[i] == vt[i - 1] && i > 1) continue;
        Slot& s = g_slots[g_used]; sprintf_s(s.name, "%s::vf%d(+%x)", label, i, static_cast<unsigned>(f - base));
        if (MH_CreateHook(f, g_thunks[g_used], &s.original) == MH_OK && MH_EnableHook(f) == MH_OK) ++g_used;
        else Emit("[ForgeShim] XBLW cannot hook %s\n", s.name);
    }
}

static void HookRva(const wchar_t* moduleName, unsigned rva, const char* label) {
    HMODULE m = GetModuleHandleW(moduleName); if (!m || g_used >= kMaxSlots) return;
    void* f = reinterpret_cast<BYTE*>(m) + rva; Slot& s = g_slots[g_used]; sprintf_s(s.name, "%s(+%x)", label, rva);
    if (MH_CreateHook(f, g_thunks[g_used], &s.original) == MH_OK && MH_EnableHook(f) == MH_OK) ++g_used; else Emit("[ForgeShim] XBLW cannot hook %s\n", s.name);
}

static std::atomic<int> g_exceptions{0};
static void ModuleRva(void* addr, char* out, size_t cap) {
    HMODULE m = nullptr; char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(addr), &m) && m) {
        GetModuleFileNameA(m, path, MAX_PATH); const char* n = strrchr(path, '\\'); sprintf_s(out, cap, "%s+%llx", n ? n + 1 : path, static_cast<unsigned long long>(static_cast<BYTE*>(addr) - reinterpret_cast<BYTE*>(m)));
    } else sprintf_s(out, cap, "%p", addr);
}
static void ThrowTypeName(EXCEPTION_RECORD* r, char* type, size_t cap) {
    if (r->NumberParameters < 4 || !r->ExceptionInformation[2] || !r->ExceptionInformation[3]) return;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(r->ExceptionInformation[2]), &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return;
    __try {
        auto imageBase = reinterpret_cast<BYTE*>(r->ExceptionInformation[3]); auto throwInfo = reinterpret_cast<const int*>(r->ExceptionInformation[2]);
        auto cta = reinterpret_cast<const int*>(imageBase + throwInfo[3]); int count = cta[0];
        for (int i = 0; i < count && i < 3; ++i) { auto ct = reinterpret_cast<const int*>(imageBase + cta[1 + i]); auto td = imageBase + ct[1];
            strncat_s(type, cap, reinterpret_cast<const char*>(td + 16), _TRUNCATE); strncat_s(type, cap, " ", _TRUNCATE); }
    } __except (EXCEPTION_EXECUTE_HANDLER) { strcpy_s(type, cap, "?"); }
}
static LONG CALLBACK LogException(EXCEPTION_POINTERS* ep) {
    static thread_local bool inside = false; if (inside) return EXCEPTION_CONTINUE_SEARCH;
    auto* r = ep->ExceptionRecord; DWORD code = r->ExceptionCode;
    { HMODULE self = nullptr; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&LogException), &self);
      HMODULE at = nullptr; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(r->ExceptionAddress), &at);
      if (at == self) return EXCEPTION_CONTINUE_SEARCH; }
    inside = true;
    if (code != 0xE06D7363 && code != 0x40080201 && code < 0xC0000000) { inside = false; return EXCEPTION_CONTINUE_SEARCH; }
    if (code == 0x406D1388) { inside = false; return EXCEPTION_CONTINUE_SEARCH; }
    // C++ rethrows ("throw;") carry no type information; logging them during unwinding upset wpnapps (fast fail).
    static const bool cpp = [] { wchar_t v[8]{}; GetEnvironmentVariableW(L"FORGE_TRACE_EXC", v, 8); return v[0] == L'c'; }();   // FORGE_TRACE_EXC=cpp also logs C++ throws
    if (code == 0xE06D7363 && (!cpp || r->NumberParameters < 4 || !r->ExceptionInformation[2])) { inside = false; return EXCEPTION_CONTINUE_SEARCH; }
    char type[200] = ""; if (code == 0xE06D7363) ThrowTypeName(r, type, sizeof(type));
    // D3D12Core throws and catches _com_error internally on every rejected pipeline; keep a few, not the cap.
    static volatile long comErrors = 0; if (strstr(type, "_com_error") && InterlockedIncrement(&comErrors) > 3) { inside = false; return EXCEPTION_CONTINUE_SEARCH; }
    int n = ++g_exceptions; if (n > 300) { inside = false; return EXCEPTION_CONTINUE_SEARCH; }
    char where[160]; ModuleRva(r->ExceptionAddress, where, sizeof(where));
    void* frames[24]{}; USHORT got = RtlCaptureStackBackTrace(0, 24, frames, nullptr);
    char stack[1400] = ""; int shown = 0;
    for (USHORT i = 0; i < got && shown < 10; ++i) { char f[160]; ModuleRva(frames[i], f, sizeof(f));
        if (strstr(f, "ForgeDesktopShim") || strstr(f, "ntdll") || strstr(f, "KERNELBASE")) continue; strncat_s(stack, f, _TRUNCATE); strncat_s(stack, " < ", _TRUNCATE); ++shown; }
    Emit("[ForgeShim] EXC %llu #%d code=%08lX at %s type=%s tid=%lu stack: %s\n", GetTickCount64() % 1000000, n, code, where, type, GetCurrentThreadId(), stack);
    inside = false; return EXCEPTION_CONTINUE_SEARCH;
}

void InstallWrapperTrace() {
    if (!GetEnvironmentVariableW(L"FORGE_TRACE_WRAPPER", nullptr, 0)) return;
    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) { Emit("[ForgeShim] XBLW MinHook init failed\n"); return; }
    HookVtable(L"XboxLiveServicesWrapperLib_release_uwp.dll", "?Get@UserFacade@XboxLiveServicesWrapper@i343@@SAAEAV123@XZ", "UserFacade");
    HookModule(L"XboxLiveServicesWrapperLib_release_uwp.dll");
    HookModule(L"ServiceLink_release_uwp.dll");
    if (GetEnvironmentVariableW(L"FORGE_TRACE_EXC", nullptr, 0)) AddVectoredExceptionHandler(1, LogException);
    const wchar_t* W = L"XboxLiveServicesWrapperLib_release_uwp.dll";
    HookRva(W, 0x1553E0, "SignInResultHandler"); HookRva(W, 0x1564E0, "UserStateNotify"); HookRva(W, 0x1571F0, "OnSignedIn");
    Emit("[ForgeShim] XBLW %d hooks\n", g_used);
}
