// Offline-services patches on Halo's own DLLs (never halo5forge.exe): make the Xbox Live
// connectivity checks report "connected" so the title's CheckIsOnline() gate passes with the
// offline user from WebAuth.cpp. Disable with FORGE_NO_ONLINE_PATCH=1.
#include <windows.h>
#include <cstdio>
#include <atomic>
#include "../third_party/minhook/include/MinHook.h"

static void Trace(const char* msg) { OutputDebugStringA("[ForgeShim] Offline "); OutputDebugStringA(msg); OutputDebugStringA("\n"); }

// Overwrites an exported function's start with a fixed body (used for the offline switches below).
static bool PatchBody(HMODULE module, const char* exportName, const char* label, const BYTE* body, size_t size, const char* result) {
    auto fn = reinterpret_cast<BYTE*>(GetProcAddress(module, exportName)); char line[200];
    if (!fn) { sprintf_s(line, "%s: export not found", label); Trace(line); return false; }
    DWORD old = 0; if (!VirtualProtect(fn, size, PAGE_EXECUTE_READWRITE, &old)) { sprintf_s(line, "%s: VirtualProtect failed %lu", label, GetLastError()); Trace(line); return false; }
    memcpy(fn, body, size); VirtualProtect(fn, size, old, &old); FlushInstructionCache(GetCurrentProcess(), fn, size);
    sprintf_s(line, "%s -> %s", label, result); Trace(line); return true;
}
// FORGE_FORCE_OFFLINE=1: the whole game consistently offline. Xbox Live connectivity and 343's ServiceLink both
// report "not connected" and ServiceLink never goes online, so menus take their offline paths (Guardians main menu:
// k_OfflineCampaign; lobby: local session) instead of trying 343's lobby/fireteam services first.
static void ForceOffline() {
    static const BYTE no[] = { 0x31, 0xC0, 0xC3 };   // xor eax, eax ; ret
    static const BYTE nothing[] = { 0xC3 };          // ret
    if (HMODULE w = GetModuleHandleW(L"XboxLiveServicesWrapperLib_release_uwp.dll")) {
        PatchBody(w, "?HasXboxLIVEConnection@XboxLiveServicesEngineInterface@XboxLiveServicesWrapper@i343@@QEAA_NXZ", "HasXboxLIVEConnection", no, sizeof(no), "false");
        PatchBody(w, "?hasInternetConnection@XboxLiveServicesEngineInterface@XboxLiveServicesWrapper@i343@@QEAA_NXZ", "hasInternetConnection", no, sizeof(no), "false");
    } else Trace("force offline: wrapper DLL not loaded");
    if (HMODULE sl = GetModuleHandleW(L"ServiceLink_release_uwp.dll")) {
        auto goOffline = reinterpret_cast<void (*)()>(GetProcAddress(sl, "?GoOffline@ServiceLink@i343@@YAXXZ")); if (goOffline) goOffline();
        PatchBody(sl, "?GoOnline@ServiceLink@i343@@YAXXZ", "ServiceLink::GoOnline", nothing, sizeof(nothing), "ignored");
        PatchBody(sl, "?IsOnline@ServiceLink@i343@@YA_NXZ", "ServiceLink::IsOnline", no, sizeof(no), "false");
    } else Trace("force offline: ServiceLink DLL not loaded");
}
static bool PatchReturnTrue(HMODULE module, const char* exportName, const char* label) {
    auto fn = reinterpret_cast<BYTE*>(GetProcAddress(module, exportName));
    char line[200];
    if (!fn) { sprintf_s(line, "%s: export not found", label); Trace(line); return false; }
    static const BYTE body[] = { 0xB0, 0x01, 0xC3 };   // mov al, 1 ; ret
    DWORD old = 0;
    if (!VirtualProtect(fn, sizeof(body), PAGE_EXECUTE_READWRITE, &old)) { sprintf_s(line, "%s: VirtualProtect failed %lu", label, GetLastError()); Trace(line); return false; }
    memcpy(fn, body, sizeof(body)); VirtualProtect(fn, sizeof(body), old, &old); FlushInstructionCache(GetCurrentProcess(), fn, sizeof(body));
    sprintf_s(line, "%s -> true", label); Trace(line); return true;
}

// UserFacade (wrapper singleton) vtable slot 49 sets byte [this+0x22D]; when set, the facade
// reports an engaged local user (slot 26 true, slot 29 -> built-in id 0x1E0F3). FORGE_USER_BYPASS=1.
static void ApplyUserBypass(HMODULE w) {
    wchar_t mode[16]{}; if (!GetEnvironmentVariableW(L"FORGE_USER_BYPASS", mode, 16) || !_wcsicmp(mode, L"late")) return;
    using GetFn = void* (*)();
    auto get = reinterpret_cast<GetFn>(GetProcAddress(w, "?Get@UserFacade@XboxLiveServicesWrapper@i343@@SAAEAV123@XZ"));
    void* obj = get ? get() : nullptr; if (!obj) { Trace("UserFacade unavailable"); return; }
    auto vt = *reinterpret_cast<void***>(obj);
    auto expected = reinterpret_cast<BYTE*>(w) + 0x68F50;
    if (vt[49] != expected) { Trace("UserFacade slot 49 is not the expected setter; skipping"); return; }
    reinterpret_cast<void(*)(void*, bool)>(vt[49])(obj, true);
    Trace("UserFacade bypass flag (+0x22D) set");
}

// Slot 38 = IsInputDeviceEngaged(deviceId). On a single-user PC every local input device belongs to
// the engaged user, so answer true whenever slot 28 (engaged user count) is non-zero.
static void* g_isDeviceEngagedOriginal = nullptr;
static void* g_engagedCount = nullptr;
static bool IsDeviceEngagedHook(void* self, unsigned long long device) {
    if (reinterpret_cast<bool (*)(void*, unsigned long long)>(g_isDeviceEngagedOriginal)(self, device)) return true;
    return reinterpret_cast<int (*)(void*)>(g_engagedCount)(self) > 0;
}
static void HookDeviceEngagement(HMODULE w, void** vt) {
    if (g_isDeviceEngagedOriginal) return;
    auto target = reinterpret_cast<BYTE*>(w) + 0x6B900; if (vt[38] != target) { Trace("slot 38 unexpected; not hooked"); return; }
    g_engagedCount = vt[28];
    MH_STATUS st = MH_Initialize(); if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) { Trace("MinHook init failed"); return; }
    if (MH_CreateHook(target, reinterpret_cast<void*>(&IsDeviceEngagedHook), &g_isDeviceEngagedOriginal) == MH_OK && MH_EnableHook(target) == MH_OK) Trace("slot 38 (IsInputDeviceEngaged) -> any local device counts");
}

// UserManager::CheckUserPrivilege(this, userIndex, privilege, showUI) -> 0 = allowed, 2/8 = denied.
// Offline there is no privilege list from Xbox Live, so every privilege is granted. FORGE_ENGAGE implies it.
static void* g_checkPrivOriginal = nullptr;
static unsigned short CheckPrivilegeHook(void* self, unsigned index, unsigned short privilege, unsigned char showUI) {
    unsigned short original = reinterpret_cast<unsigned short (*)(void*, unsigned, unsigned short, unsigned char)>(g_checkPrivOriginal)(self, index, privilege, showUI);
    char l[120]; sprintf_s(l, "privilege %u for user %u: game said %u -> granted", privilege, index, original); Trace(l);
    return 0;
}
static void HookPrivileges(HMODULE w) {
    if (g_checkPrivOriginal) return;
    void* target = reinterpret_cast<BYTE*>(w) + 0x155DB0;
    MH_STATUS st = MH_Initialize(); if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return;
    if (MH_CreateHook(target, reinterpret_cast<void*>(&CheckPrivilegeHook), &g_checkPrivOriginal) == MH_OK && MH_EnableHook(target) == MH_OK) Trace("CheckUserPrivilege hooked (offline: all privileges granted)");
}

// Engage the signed-in PC user (UserManager id 'ausr' = 0x61757372) as active user index 0 via the
// facade's own SetEngagedUserId (vtable slot 40), once sign-in has completed. FORGE_ENGAGE=1.
static void StartEngagement(HMODULE w) {
    if (!GetEnvironmentVariableW(L"FORGE_ENGAGE", nullptr, 0)) return;
    CreateThread(nullptr, 0, [](void* param) -> DWORD {
        auto w = static_cast<HMODULE>(param);
        using GetFn = void* (*)(); using HasFn = bool (*)();
        auto get = reinterpret_cast<GetFn>(GetProcAddress(w, "?Get@UserFacade@XboxLiveServicesWrapper@i343@@SAAEAV123@XZ"));
        auto has = reinterpret_cast<HasFn>(GetProcAddress(w, "?UserManager_HasSignedInUser@XboxLiveServicesEngineInterfaceInternal@details@XboxLiveServicesWrapper@i343@@SA_NXZ"));
        if (!get || !has) { Trace("engage: exports missing"); return 1; }
        // Never touch the user manager before the game initialises the wrapper; wait for its
        // first sign-in attempt instead (OfflineSignInStarted is set by WebAuth.cpp).
        extern volatile long g_offlineTokenRequests;
        for (int i = 0; i < 1800 && g_offlineTokenRequests == 0; ++i) Sleep(100);
        if (g_offlineTokenRequests == 0) { Trace("engage: game never requested an Xbox Live token"); return 1; }
        wchar_t d[16]{}; DWORD delay = GetEnvironmentVariableW(L"FORGE_ENGAGE_DELAY", d, 16) ? wcstoul(d, nullptr, 10) : 2000;   // ms after the first token request
        Sleep(delay);
        for (int i = 0; i < 6000 && !has(); ++i) Sleep(10);
        if (!has()) { Trace("engage: no signed-in user after 60 s"); return 1; }
        void* obj = get(); auto vt = *reinterpret_cast<void***>(obj);
        if (vt[40] != reinterpret_cast<BYTE*>(w) + 0x6C200) { Trace("engage: slot 40 is not SetEngagedUserId; skipping"); return 1; }
        HookDeviceEngagement(w, vt);
        using EngageFn = void (*)(void*, int, unsigned, unsigned long long, void*);
        reinterpret_cast<EngageFn>(vt[40])(obj, 0, 0x61757372u, 0ull, nullptr);
        using IsSignedFn = bool (*)(void*, int); using IdFn = unsigned (*)(void*, int);
        char line[160]; sprintf_s(line, "engage: index0 -> 'ausr'; signedIn(0)=%d engagedId(0)=%x", reinterpret_cast<IsSignedFn>(vt[9])(obj, 0), reinterpret_cast<IdFn>(vt[29])(obj, 0));
        Trace(line);
        wchar_t mode[16]{}; GetEnvironmentVariableW(L"FORGE_USER_BYPASS", mode, 16);
        if (!_wcsicmp(mode, L"late") && vt[49] == reinterpret_cast<BYTE*>(w) + 0x68F50) { reinterpret_cast<void(*)(void*, bool)>(vt[49])(obj, true); Trace("engage: bypass flag set after engagement"); }
        return 0;
    }, w, 0, nullptr);
}

// FORGE_AUTOSTART=1: press the virtual gamepad's Start (Menu) every 5 s from 20 s after launch until the
// game requests its first Xbox Live token (= title continued into sign-in), for unattended launches.
extern std::atomic<unsigned> g_padSynth;
static void StartAutoStart() {
    wchar_t v[8]{}; if (!GetEnvironmentVariableW(L"FORGE_AUTOSTART", v, 8) || v[0] == L'0') return;
    CreateThread(nullptr, 0, [](void*) -> DWORD {
        extern volatile long g_offlineTokenRequests;
        Sleep(20000);
        for (int i = 0; i < 60 && g_offlineTokenRequests == 0; ++i) {
            g_padSynth |= 0x1u; Sleep(250); g_padSynth &= ~0x1u; Sleep(4750);
        }
        Trace(g_offlineTokenRequests ? "autostart: title continued (sign-in started)" : "autostart: gave up after 5 minutes");
        return 0;
    }, nullptr, 0, nullptr);
}

void ApplyOfflinePatches() {
    StartAutoStart();
    if (GetEnvironmentVariableW(L"FORGE_FORCE_OFFLINE", nullptr, 0)) ForceOffline();
    if (GetEnvironmentVariableW(L"FORGE_SERVICELINK_OFFLINE", nullptr, 0)) {
        if (HMODULE sl = GetModuleHandleW(L"ServiceLink_release_uwp.dll")) {
            auto goOffline = reinterpret_cast<void (*)()>(GetProcAddress(sl, "?GoOffline@ServiceLink@i343@@YAXXZ"));
            auto isOnline = reinterpret_cast<bool (*)()>(GetProcAddress(sl, "?IsOnline@ServiceLink@i343@@YA_NXZ"));
            if (goOffline) { goOffline(); char l[80]; sprintf_s(l, "ServiceLink::GoOffline() called; IsOnline=%d", isOnline ? isOnline() : -1); Trace(l); }
        }
    }
    if (HMODULE w = GetModuleHandleW(L"XboxLiveServicesWrapperLib_release_uwp.dll")) { if (GetEnvironmentVariableW(L"FORGE_ENGAGE", nullptr, 0)) HookPrivileges(w); StartEngagement(w); }
    if (HMODULE w = GetModuleHandleW(L"XboxLiveServicesWrapperLib_release_uwp.dll")) ApplyUserBypass(w);
    if (!GetEnvironmentVariableW(L"FORGE_ONLINE_PATCH", nullptr, 0)) return;   // opt-in: did not affect the sign-in gate
    HMODULE w = GetModuleHandleW(L"XboxLiveServicesWrapperLib_release_uwp.dll");
    if (!w) { Trace("wrapper DLL not loaded"); return; }
    PatchReturnTrue(w, "?HasXboxLIVEConnection@XboxLiveServicesEngineInterface@XboxLiveServicesWrapper@i343@@QEAA_NXZ", "HasXboxLIVEConnection");
    PatchReturnTrue(w, "?hasInternetConnection@XboxLiveServicesEngineInterface@XboxLiveServicesWrapper@i343@@QEAA_NXZ", "hasInternetConnection");
}
