// Campaign engine settings for standalone Forge, applied when the shim initializes (process still suspended,
// before any game code runs). Both use switches the engine already has; nothing is loaded or replayed later.
//
// FORGE_OFFLINE_LOBBY=1  Boot the network lifecycle in its "offlinelobby" mode (engine enum Lifecycle_Mode:
//   0 onlinelobby, 1 offlinelobby, 2 oldlifecycle; the dev command NetClientSetLifecycleMode / init.txt sets it).
//   The LobbyServiceManager reads the mode once at init (halo5forge+0x2081d10) and creates OfflineLobbySession
//   instead of the 343 lobby-service session, so Solo -> k_OfflineCampaign runs the engine's own local campaign
//   lobby (offlinelobbysession.cpp / offlinegamesession.cpp) with no "connecting to lobby" round trips.
//   Done the way the engine does it before init: write the pending mode at c_life_cycle_state_manager+0x1a44.
//
// FORGE_UI_RETAIN=1  Keep the pause menu / HUD UI closure resident across the menu -> mission transition. At each
//   scenario transition the tag cache flags every loaded tag 0x8000 ("unload unless the new scenario needs it",
//   halo5forge+0x29eec30). The campaign missions were built against Guardians' globals, so Forge's pause menu
//   (ui\...\visor_menus\pause_menu.lua_script, ausar_visor_menus.node_graph_stack, ...) and the shared UI string
//   lists are not in their closure and get unloaded: no pause UI, and a crash in the first object string lookup
//   ('unic' tag missing, halo5forge+abb21f). The list of tags to keep is baked by h5bake from halo5onpc's prepared
//   UI residency set into <Forge root>\h5campaign\ui-retain.bin; after the native marking pass those records get
//   0x8000 cleared again (the same rule halo5onpc applies: only records flagged 0xa000).
#include <windows.h>
#include <atomic>
extern std::atomic<unsigned> g_padSynth;   // Gamepad.cpp: virtual pad buttons held by the shim
#include <intrin.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>
#include "../third_party/minhook/include/MinHook.h"

namespace {
uintptr_t g_base = 0;
void Log(const char* fmt, ...) { char b[400]; va_list a; va_start(a, fmt); _vsnprintf_s(b, _TRUNCATE, fmt, a); va_end(a); OutputDebugStringA("[ForgeShim] ENGINE "); OutputDebugStringA(b); OutputDebugStringA("\n"); }

bool Identity() {
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr)); if (!base) return false;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base); if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != 1520325666) return false;   // Halo 5: Forge 1.194.6192.2
    g_base = reinterpret_cast<uintptr_t>(base); return true;
}
bool Bytes(uintptr_t rva, const char* hex) {
    auto p = reinterpret_cast<const unsigned char*>(g_base + rva);
    for (size_t i = 0; hex[i * 2]; ++i) { unsigned v = 0; sscanf_s(hex + i * 2, "%2x", &v); if (p[i] != v) return false; }
    return true;
}
bool Hook(uintptr_t rva, void* detour, void** original, const char* what) {
    MH_STATUS st = MH_Initialize(); if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return false;
    void* target = reinterpret_cast<void*>(g_base + rva);
    if (MH_CreateHook(target, detour, original) != MH_OK || MH_EnableHook(target) != MH_OK) { Log("%s: hook failed", what); return false; }
    return true;
}

const char* SessionKind();
void ReassertEventSink();
// ---- offline lobby -----------------------------------------------------------------------------------
constexpr uintptr_t kLobbyManagerInit = 0x2081d10;    // LobbyServiceManager init: mode = lifecycle[+0x2b8+0x178c]
constexpr uintptr_t kLifecycleManager = 0x2079720;    // returns c_life_cycle_state_manager* (null before init)
constexpr uintptr_t kLobbyManagerMode = 0x641b314;    // LobbyServiceManager global (+4 = mode)
using VoidFn = void(*)();
VoidFn g_lobbyInit = nullptr;
void LobbyInit() {
    ReassertEventSink();
    auto lifecycle = reinterpret_cast<unsigned char*(*)()>(g_base + kLifecycleManager)();
    if (lifecycle) { auto& mode = *reinterpret_cast<int*>(lifecycle + 0x1a44); Log("lifecycle mode %d -> 1 (offlinelobby)", mode); mode = 1; }
    else Log("lifecycle manager not ready; lobby mode left to the engine");
    g_lobbyInit();
    Log("lobby service manager mode now %d, session %s", *reinterpret_cast<int*>(g_base + kLobbyManagerMode), SessionKind());
}


// Lifecycle change requests after init (halo5forge+0x2082340: manager, out, mode, flag). Logged; with
// the caller stack (diagnostic; the request is passed through unchanged).
constexpr uintptr_t kLobbyModeChange = 0x2082340;
using ModeFn = void*(*)(void*, void*, int, char);
ModeFn g_modeChange = nullptr; bool g_keepOffline = false; bool g_inOfflineCampaign = false;
void* ModeChange(void* manager, void* out, int mode, char flag) {
    ReassertEventSink();
    void* frames[24]{}; USHORT got = RtlCaptureStackBackTrace(1, 24, frames, nullptr); char stack[600] = ""; size_t n = 0;
    for (USHORT i = 0; i < got && n < 560; ++i) { auto a = reinterpret_cast<uintptr_t>(frames[i]); n += sprintf_s(stack + n, sizeof(stack) - n, a >= g_base && a < g_base + 0x7000000 ? " +%llx" : " ?", static_cast<unsigned long long>(a - g_base)); }
    Log("lifecycle change requested: %d -> %d (flag %d); from%s", *reinterpret_cast<int*>(g_base + kLobbyManagerMode), mode, flag, stack);
    // OfflineLobbySession's queued "go online" operation (halo5forge+0x25e0000: unconditionally requests onlinelobby
    // once the signed-in user's online party is seen). That is what moved Solo from the engine's offline campaign
    // lobby to 343's lobby service ("connecting to lobby", then "unable to join fireteam"). Keep the offline session.
    uintptr_t from = reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_base;
    // Lobby activity requests (Solo -> offline, main menu / multiplayer -> online) decide whether we are in the
    // offline campaign lobby.
    if (from == 0x2140367) g_inOfflineCampaign = mode == 1;
    if (g_keepOffline && mode == 0 && from == 0x25e002b) {
        memset(out, 0, 16); Log("offline lobby session asked to go online; kept offline");
        return out;
    }
    // The signed-in-user path (+0x21aac20, e.g. when the sign-in prompt completes) also requests onlinelobby. At
    // startup the main menu needs that; inside the offline campaign lobby it flips Solo to Co-op ("unable to join
    // fireteam"). Refused only while in the offline campaign lobby.
    if (g_keepOffline && mode == 0 && from == 0x21aad67 && g_inOfflineCampaign) {
        // Answered as a request for the mode we are already in, so the caller gets the engine's normal
        // "nothing to change" result instead of an empty one (an empty one ends in "unable to join").
        Log("signed-in user path asked to go online inside the offline campaign lobby; answered with offlinelobby");
        return g_modeChange(manager, out, 1, flag);
    }
    return g_modeChange(manager, out, mode, flag);
}
const char* SessionKind() {
    auto session = *reinterpret_cast<unsigned char**>(g_base + kLobbyManagerMode - 4 + 0x20);
    if (!session) return "none";
    auto vt = *reinterpret_cast<uintptr_t*>(session) - g_base;
    static char text[64]; sprintf_s(text, "%s (vtable +%llx)", vt == 0x3912db0 ? "OfflineLobbySession" : "other", static_cast<unsigned long long>(vt)); return text;
}

// Game variant lookup by id (halo5forge+0x2342b60: this, const GUID* id, out, ...). In the offline lobby nobody
// sets the campaign variant id (343's lobby service does that online), so the lookup gets the empty GUID and the
// pre-game step stops with "game start status has an error [4]". The engine's own local-file route maps
// 708d6f0d-12fe-42ce-9287-ca01cba99a0e -> __cms__\campaign\campaignnormal.bin; with the offline lobby active an
// empty id is given that id. Every lookup is logged (first 40) so the effect is visible.
constexpr uintptr_t kVariantById = 0x2342b60;
using VariantFn = void*(*)(void*, const GUID*, void*, void*);
VariantFn g_variantById = nullptr; volatile long g_variantLogs = 0;
const GUID kCampaignNormal = { 0x708d6f0d, 0x12fe, 0x42ce, { 0x92, 0x87, 0xca, 0x01, 0xcb, 0xa9, 0x9a, 0x0e } };
void* VariantById(void* self, const GUID* id, void* out, void* extra) {
    static const GUID zero{}; bool empty = id && memcmp(id, &zero, sizeof(GUID)) == 0; int mode = *reinterpret_cast<int*>(g_base + kLobbyManagerMode);
    bool substitute = empty && mode == 1;
    if (InterlockedIncrement(&g_variantLogs) <= 40)
        Log("game variant lookup %08lx-%04x-%04x... (lobby mode %d) from +%llx%s", id ? id->Data1 : 0, id ? id->Data2 : 0, id ? id->Data3 : 0, mode,
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_base), substitute ? " -> campaignnormal" : "");
    return g_variantById(self, substitute ? &kCampaignNormal : id, out, extra);
}

// ---- campaign start diagnostics (FORGE_CAMPAIGN_TRACE=1) ----------------------------------------------
// The offline host refuses to start with game start error 4: its players' map precache status (+0x2211e80)
// is "none"/"failed". These log the inputs to that decision when they change.
using SetMissionFn = void(*)(int); SetMissionFn g_setMission = nullptr;
void SetMission(int mission) { Log("campaign: SetMissionId(%d) from +%llx", mission, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_base)); g_setMission(mission); }
using MapParamFn = bool(*)(int*, int*, int*, int*); MapParamFn g_mapParam = nullptr; int g_lastMap[4] = { -2, -2, -2, -2 }; bool g_lastMapOk = false;
bool MapParam(int* a, int* b, int* c, int* d) {
    bool ok = g_mapParam(a, b, c, d);
    int now[4] = { *a, *b, *c, *d };
    if (ok != g_lastMapOk || memcmp(now, g_lastMap, sizeof now)) { memcpy(g_lastMap, now, sizeof now); g_lastMapOk = ok; Log("campaign: session map parameter ok=%d values %d %d %d %d", ok, now[0], now[1], now[2], now[3]); }
    return ok;
}
using PrecacheFn = int(*)(int, int*, int*); PrecacheFn g_precache = nullptr; int g_lastPre = -99, g_lastPreMap = -99;
int Precache(int map, int* progress, int* other) {
    int r = g_precache(map, progress, other);
    if (r != g_lastPre || map != g_lastPreMap) { g_lastPre = r; g_lastPreMap = map; Log("campaign: players' precache status for map %d = %d (progress %d)", map, r, progress ? *progress : -1); }
    return r;
}
void InstallCampaignTrace() {
    Hook(0x214b690, reinterpret_cast<void*>(&SetMission), reinterpret_cast<void**>(&g_setMission), "SetMissionId");
    Hook(0x2289580, reinterpret_cast<void*>(&MapParam), reinterpret_cast<void**>(&g_mapParam), "map parameter");
    Hook(0x2211e80, reinterpret_cast<void*>(&Precache), reinterpret_cast<void**>(&g_precache), "precache status");
    Log("campaign start trace installed");
}

// ---- UI residency ------------------------------------------------------------------------------------
constexpr uintptr_t kMarkForUnload = 0x29eec30;       // flags every cached tag 0x8000 at a scenario transition
constexpr uintptr_t kTagPool = 0x5f20768;             // tag cache datum array: +0x20 stride (88), +0x4c count, +0x58 rows
std::vector<uint32_t> g_keep;
VoidFn g_mark = nullptr;
long g_transitions = 0;
// Set when the engine reports the player quitting the mission ("simulation aborted, reason='exiting-in-game'"):
// the transition that follows goes back to the main menu, whose own UI is complete. Keeping the mission-time UI
// set across that one left the main menu's font lists pointing at unloaded tag data (a crash on the PS5 when
// returning to the menu, halo5forge+1105054), so that transition is left to the engine.
volatile LONG g_leavingMission = 0;
void KeepResident() {
    auto pool = *reinterpret_cast<unsigned char**>(g_base + kTagPool);
    if (!pool || *reinterpret_cast<uint64_t*>(pool + 0x20) != 88) return;
    auto count = *reinterpret_cast<uint32_t*>(pool + 0x4c); auto rows = *reinterpret_cast<unsigned char**>(pool + 0x58);
    if (!rows || count > 0x15400) return;
    unsigned kept = 0, absent = 0;
    for (uint32_t i = 0; i < count; ++i) {
        auto row = rows + i * 88ull; auto gid = *reinterpret_cast<uint32_t*>(row + 76); auto root = *reinterpret_cast<unsigned char**>(row + 40);
        if (!root || gid == 0xffffffff || !std::binary_search(g_keep.begin(), g_keep.end(), gid)) continue;
        if (*reinterpret_cast<uint32_t*>(root + 8) != gid) { ++absent; continue; }
        auto& flags = *reinterpret_cast<uint16_t*>(row + 80);
        if ((flags & 0xa000) == 0xa000) { flags &= 0x7fff; ++kept; }
    }
    Log("transition %ld: kept %u of %zu UI tags resident (%u mismatched); lobby mode %d, session %s", g_transitions, kept, g_keep.size(), absent, *reinterpret_cast<int*>(g_base + kLobbyManagerMode), SessionKind());
}
void Mark() {
    g_mark();
    ++g_transitions; ReassertEventSink();
    if (InterlockedExchange(&g_leavingMission, 0)) { Log("transition %ld: leaving the mission for the main menu; UI tags left to the engine", g_transitions); return; }
    __try { KeepResident(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("residency pass faulted; skipped"); }
}
// ui-retain.bin (halo5onpc UiResidency): u32 0x49553548, u32 1, string package, u32 n, n x (u32 gid, u64 asset,
// u64 checksum), u32 m, m x (u32 gid, u64 asset, u64 checksum). Strings are i32 length + UTF-8.
bool LoadKeepList() {
    wchar_t path[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, path); wcscat_s(path, L"\\h5campaign\\ui-retain.bin");
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) { Log("no %ls", path); return false; }
    LARGE_INTEGER size{}; GetFileSizeEx(f, &size); std::vector<unsigned char> d(size.QuadPart > 0 && size.QuadPart < (1 << 20) ? static_cast<size_t>(size.QuadPart) : 0);
    DWORD got = 0; bool ok = !d.empty() && ReadFile(f, d.data(), static_cast<DWORD>(d.size()), &got, nullptr) && got == d.size(); CloseHandle(f);
    size_t at = 0; auto u32 = [&](uint32_t& v) { if (at + 4 > d.size()) return false; memcpy(&v, &d[at], 4); at += 4; return true; };
    uint32_t magic = 0, version = 0, len = 0, n = 0;
    ok = ok && u32(magic) && u32(version) && magic == 0x49553548 && version == 1 && u32(len) && at + len <= d.size();
    if (ok) { static const char pkg[] = "Microsoft.Halo5Forge_1.194.6192.2_x64__8wekyb3d8bbwe"; ok = len == sizeof(pkg) - 1 && memcmp(&d[at], pkg, len) == 0; at += len; }
    for (int list = 0; ok && list < 2; ++list) {
        ok = u32(n) && n <= 16384 && at + n * 20ull <= d.size();
        for (uint32_t i = 0; ok && i < n; ++i) { uint32_t gid; memcpy(&gid, &d[at], 4); at += 20; g_keep.push_back(gid); }
    }
    ok = ok && at == d.size();
    if (!ok) { g_keep.clear(); Log("ui-retain.bin is damaged or for another Forge build"); return false; }
    std::sort(g_keep.begin(), g_keep.end()); g_keep.erase(std::unique(g_keep.begin(), g_keep.end()), g_keep.end());
    Log("UI residency list: %zu tags", g_keep.size()); return true;
}

// ---- engine event log (FORGE_ENGINE_LOG=1 warnings+errors, =all everything) --------------------------
// The engine's event logger (halo5forge+0x6569d0, 10k call sites) forwards (file, line, level, category, fmt,
// va_list) to a sink pointer that retail builds leave unset. Point it at OutputDebugString so the game's own
// diagnostics (lobby, lifecycle, campaign start) land in game-debug.log.
constexpr uintptr_t kEventEnabled = 0x58b4a48, kEventSink = 0x58b4a50;
bool g_logAll = false; bool g_cats[256] = {}; bool g_skip[256] = {}; volatile long g_logged = 0;
void EventSink(const char* file, int line, int level, int category, const char* fmt, va_list args) {
    if (category >= 0 && category < 256 && g_skip[category]) return;
    if (!g_logAll && level > 3 && !(category >= 0 && category < 256 && g_cats[category])) return;   // L1-L3 = errors/warnings/notices; L4+ is per-frame detail
    if (InterlockedIncrement(&g_logged) > 1500000) return;
    char text[1024]; text[0] = 0;
    __try { _vsnprintf_s(text, _TRUNCATE, fmt ? fmt : "", args); } __except (EXCEPTION_EXECUTE_HANDLER) { strcpy_s(text, fmt ? fmt : "?"); }
    if (strstr(text, "simulation aborted") && strstr(text, "exiting-in-game")) InterlockedExchange(&g_leavingMission, 1);
    const char* leaf = file ? strrchr(file, '\\') : nullptr; leaf = leaf ? leaf + 1 : (file ? file : "?");
    char out[1200]; sprintf_s(out, "[H5] L%d c%d %s:%d %s\n", level, category, leaf, line, text); OutputDebugStringA(out);
}
}

namespace {
bool g_eventsOn = false;
void ReassertEventSink() {
    if (!g_eventsOn) return;
    auto& sink = *reinterpret_cast<void**>(g_base + kEventSink); auto& on = *reinterpret_cast<bool*>(g_base + kEventEnabled);
    if (sink != reinterpret_cast<void*>(&EventSink) || !on) { Log("event log sink was reset by the game (sink %p, on %d); restored", sink, on); sink = reinterpret_cast<void*>(&EventSink); on = true; }
}
}

// ---- offline error popups --------------------------------------------------------------------------
// With no Microsoft account the Halo Waypoint spartan token can't be fetched, and the UI error manager posts
// "Error authorizing user with Halo 5: Forge services" at boot (dismissable; campaign is unaffected). Drop that one
// error (its id is the string id the game registers for "failedToGetSpartanToken", +0x63d5c00) and log the rest.
constexpr uintptr_t kErrorAdd = 0x1c9abc0;            // user_interface_error_manager add(mgr, const int* id, ...) -> added
constexpr uintptr_t kSpartanTokenErrorId = 0x63d5c00;
constexpr int kProfileSyncErrorId = 0x6ea82a5c;    // string id hash of the profile sync error (this build)
using ErrorAddFn = bool(*)(void*, const int*, void*, void*, unsigned long long, unsigned long long, unsigned long long, unsigned long long);
ErrorAddFn g_errorAdd = nullptr;
bool ErrorAdd(void* mgr, const int* id, void* a, void* b, unsigned long long c, unsigned long long d, unsigned long long e, unsigned long long f) {
    int spartan = *reinterpret_cast<int*>(g_base + kSpartanTokenErrorId);
    if (id && spartan && *id == spartan) { static bool once = false; if (!once) { once = true; Log("offline: dropped the spartan-token authorization popup"); } return false; }
    // "Profile sync error": raised when Save/preferences.gs doesn't exist yet and the cloud profile can't be reached.
    // Offline there is no cloud copy; defaults are correct and the first settings change writes the local save.
    if (id && *id == kProfileSyncErrorId) { static bool once = false; if (!once) { once = true; Log("offline: dropped the profile sync popup (no local preferences yet)"); } return false; }
    Log("UI error posted: id %08x", id ? *id : 0);
    return g_errorAdd(mgr, id, a, b, c, d, e, f);
}

// The Xbox Live wrapper's event 5 (sign-in cancelled / no user) shows "Sign-in required" (+0x1318bb0, dialog
// player_profile_sign_in_cancel). Offline, on any boot after the first, the wrapper raises it while the remembered
// offline user is still being signed in; OK then lands on the main menu signed in.
constexpr uintptr_t kSignInCancelled = 0x1318bb0;
using VoidPtrFn = void(*)(void*);
VoidPtrFn g_signInCancelled = nullptr;
// Its OK button is what retries the sign-in (skipping the handler leaves the title on "Loading"), so let the popup
// show and press A on the virtual pad once it is up.
void SignInCancelled(void* event) {
    g_signInCancelled(event);
    static LONG n = 0; LONG k = InterlockedIncrement(&n); if (k > 3) return;
    Log("offline: 'sign-in required' popup shown; confirming it");
    CloseHandle(CreateThread(nullptr, 0, [](void*) -> DWORD { Sleep(2500); g_padSynth |= 0x4u; Sleep(250); g_padSynth &= ~0x4u; return 0; }, nullptr, 0, nullptr));
}

// ---- GPU residency guard ----------------------------------------------------------------------------
// Crash seen in play (game crash report, Osiris after ~10 min): read AV at +0x15c4433 reading [entry+0xbc] with a
// null entry. The frame submit (+0x1582830) passes its per-frame resource list (std::vector<resource*> at +0x40) to
// the residency manager's MakeResident batcher (+0x15c4380, ID3D12Device::MakeResident) and then to +0x15c40f0;
// a resource released mid-frame leaves a null slot. Null is never a valid entry: drop those before either walk.
constexpr uintptr_t kResidencyMake = 0x15c4380, kResidencyTrack = 0x15c40f0;
using ResidencyFn = void*(*)(void*, void**, void*);
ResidencyFn g_resMake = nullptr, g_resTrack = nullptr;
volatile LONG g_nullsDropped = 0;
void Compact(void** vec) {
    auto begin = reinterpret_cast<void**>(vec[0]), end = reinterpret_cast<void**>(vec[1]);
    if (!begin || end <= begin) return;
    void** out = begin; for (void** p = begin; p < end; ++p) if (*p) *out++ = *p;
    if (out != end) {
        LONG n = InterlockedAdd(&g_nullsDropped, static_cast<LONG>(end - out));
        if (n <= 20 || (n & 1023) == 0) Log("residency guard: dropped %lld null resource entr%s (total %ld)", static_cast<long long>(end - out), end - out == 1 ? "y" : "ies", n);
        vec[1] = out;
    }
}
void* ResidencyMake(void* mgr, void** vec, void* x) { if (vec) Compact(vec); return g_resMake(mgr, vec, x); }
void* ResidencyTrack(void* mgr, void** vec, void* x) { if (vec) Compact(vec); return g_resTrack(mgr, vec, x); }

void InstallCampaignEngine() {
    bool lobby = GetEnvironmentVariableW(L"FORGE_OFFLINE_LOBBY", nullptr, 0) != 0, retain = GetEnvironmentVariableW(L"FORGE_UI_RETAIN", nullptr, 0) != 0;
    wchar_t logMode[8] = L""; bool events = GetEnvironmentVariableW(L"FORGE_ENGINE_LOG", logMode, 8) != 0;
    bool quiet = GetEnvironmentVariableW(L"FORGE_OFFLINE_ERRORS", nullptr, 0) != 0;
    wchar_t guardMode[4] = L""; bool guard = !(GetEnvironmentVariableW(L"FORGE_RESIDENCY_GUARD", guardMode, 4) && guardMode[0] == L'0');   // on unless =0
    if (!lobby && !retain && !events && !quiet && !guard) return;
    if (!Identity()) { Log("unsupported halo5forge.exe build; campaign engine settings not applied"); return; }
    if (guard) {
        if (Bytes(kResidencyMake, "4057415441554156") && Bytes(kResidencyTrack, "4c89442418555356") &&
            Hook(kResidencyMake, reinterpret_cast<void*>(&ResidencyMake), reinterpret_cast<void**>(&g_resMake), "residency make") &&
            Hook(kResidencyTrack, reinterpret_cast<void*>(&ResidencyTrack), reinterpret_cast<void**>(&g_resTrack), "residency track"))
            Log("residency guard: installed");
        else Log("residency guard: code differs; not applied");
    }
    if (quiet) {
        if (Bytes(kErrorAdd, "4055535657415441") && Hook(kErrorAdd, reinterpret_cast<void*>(&ErrorAdd), reinterpret_cast<void**>(&g_errorAdd), "UI errors")) Log("offline: spartan-token popup suppressed");
        else Log("offline: error manager code differs; popup not suppressed");
        if (Bytes(kSignInCancelled, "4055488dac2450ffffff") && Hook(kSignInCancelled, reinterpret_cast<void*>(&SignInCancelled), reinterpret_cast<void**>(&g_signInCancelled), "sign-in popup")) Log("offline: sign-in-required popup auto-confirmed");
        else Log("offline: sign-in popup code differs; not suppressed");
    }
    if (events && Bytes(0x6569d0, "4883ec38803d")) {
        g_logAll = _wcsicmp(logMode, L"all") == 0;
        // FORGE_ENGINE_CATS=124.84.85 : also log level-4 detail for these categories
        { wchar_t sk[200] = L""; if (GetEnvironmentVariableW(L"FORGE_ENGINE_SKIP", sk, 200)) { for (wchar_t* c = sk; *c;) { wchar_t* e = c; int v = static_cast<int>(wcstol(c, &e, 10)); if (e == c) break; if (v >= 0 && v < 256) g_skip[v] = true; c = e; while (*c == L'.') ++c; } } }
        wchar_t cats[200] = L""; if (GetEnvironmentVariableW(L"FORGE_ENGINE_CATS", cats, 200)) { for (wchar_t* c = cats; *c;) { wchar_t* e = c; int v = static_cast<int>(wcstol(c, &e, 10)); if (e == c) break; if (v >= 0 && v < 256) g_cats[v] = true; c = e; while (*c == L'.') ++c; } }
        *reinterpret_cast<void**>(g_base + kEventSink) = reinterpret_cast<void*>(&EventSink); *reinterpret_cast<bool*>(g_base + kEventEnabled) = true;
        g_eventsOn = true; CloseHandle(CreateThread(nullptr, 0, [](void*) -> DWORD { for (;;) { Sleep(250); ReassertEventSink(); } }, nullptr, 0, nullptr)); Log("engine event log -> debug log (%s)", g_logAll ? "all" : "warnings and errors");
    }
    if (lobby) {
        if (Bytes(kLobbyManagerInit, "4883ec38e8077affff4885c0") && Hook(kLobbyManagerInit, reinterpret_cast<void*>(&LobbyInit), reinterpret_cast<void**>(&g_lobbyInit), "offline lobby"))
            Log("offline lobby: lifecycle boots in offlinelobby mode");
        else Log("offline lobby: code differs; not applied");
        g_keepOffline = Bytes(0x25e0026, "e81523aaff488b5c2428");
        if (!g_keepOffline) Log("offline lobby: go-online site differs; not kept");
        if (Bytes(kVariantById, "4055565741564157") ) { if (!Hook(kVariantById, reinterpret_cast<void*>(&VariantById), reinterpret_cast<void**>(&g_variantById), "variant lookup")) Log("variant hook failed"); }
        else Log("variant lookup code differs; not hooked");
        if (!Hook(kLobbyModeChange, reinterpret_cast<void*>(&ModeChange), reinterpret_cast<void**>(&g_modeChange), "lifecycle change")) Log("lifecycle change hook failed");
    }
    if (GetEnvironmentVariableW(L"FORGE_CAMPAIGN_TRACE", nullptr, 0) && Bytes(0x2211e80, "4c89442418") && Bytes(0x2289580, "4889742410") && Bytes(0x214b690, "4057")) InstallCampaignTrace();
    if (retain && LoadKeepList()) {
        if (Bytes(kMarkForUnload, "48895c2410574883ec40488b15") && Hook(kMarkForUnload, reinterpret_cast<void*>(&Mark), reinterpret_cast<void**>(&g_mark), "UI residency"))
            Log("UI residency: installed");
        else Log("UI residency: code differs; not applied");
    }
}

// Test automation (FORGE_LIVEKEYS "M<n>"): select campaign mission n in the lobby, as the mission list does
// (networklobbysessioninterface SetMissionId, halo5forge+0x214b690).
void CampaignSelectMission(int mission) {
    if (!g_base && !Identity()) return;
    // (may already be hooked by the campaign trace, so no byte check; the call goes through the hook)
    reinterpret_cast<void(*)(int)>(g_base + 0x214b690)(mission);
    Log("automation: SetMissionId(%d)", mission);
}
