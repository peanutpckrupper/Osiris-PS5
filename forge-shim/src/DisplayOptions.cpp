// Display options ported from halo5onpc's CampaignDisplay helper, installed at launch instead of injected later:
//   - frame rate options 3 and 4 = 144 and 180 FPS (Forge itself knows 60 and 30): frame pacing period, present
//     interval, the "above 1 means 30 FPS" check and the 60 Hz metronome period, via four 1-byte traps (int3 + VEH)
//   - campaign field of view (60..120, default 78) as a PlayerProfile property "CampaignFieldOfView", registered on the
//     game's update thread; stored in <game>\h5campaign\display.ini [Campaign] FieldOfView
// The settings entries themselves come from the baked audio/video settings script (tag luas 003d87f1, halo5onpc's
// display-ui recipe) in the campaign layer. FORGE_DISPLAY_OPTIONS=0 turns all of this off.
#include <windows.h>
#include <intrin.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include "../third_party/minhook/include/MinHook.h"

namespace {
uintptr_t g_base = 0;
float g_fov = 78; wchar_t g_ini[MAX_PATH]{};
volatile LONG g_registered = 0, g_paceHits = 0, g_clockHits = 0;
void Log(const char* fmt, ...) { char b[300]; va_list a; va_start(a, fmt); _vsnprintf_s(b, _TRUNCATE, fmt, a); va_end(a); OutputDebugStringA("[ForgeShim] DISPLAY "); OutputDebugStringA(b); OutputDebugStringA("\n"); }
bool Bytes(uintptr_t rva, const unsigned char* expect, size_t n) { return memcmp(reinterpret_cast<void*>(g_base + rva), expect, n) == 0; }
bool Swap(uintptr_t rva, unsigned char before, unsigned char after)
{
    auto site = reinterpret_cast<char*>(g_base + rva); DWORD old = 0, unused = 0;
    if (!VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
    bool ok = _InterlockedCompareExchange8(site, static_cast<char>(after), static_cast<char>(before)) == static_cast<char>(before);
    FlushInstructionCache(GetCurrentProcess(), site, 1); VirtualProtect(site, 1, old, &unused); return ok;
}

// ---- field of view property ------------------------------------------------------------------------
void* GetCampaignFov(void*, void* output)
{
    auto p = static_cast<unsigned char*>(output);
    *reinterpret_cast<uint64_t*>(p) = 0; *reinterpret_cast<uint32_t*>(p + 8) = 0xffff0003;   // GenericValue scalar
    *reinterpret_cast<float*>(p) = g_fov; return output;
}
void SetCampaignFov(void*, const void* value)
{
    float f = *static_cast<const float*>(value); if (!std::isfinite(f)) return;
    f = floorf(f + 0.5f); if (f < 60) f = 60; if (f > 120) f = 120;
    g_fov = f; *reinterpret_cast<float*>(g_base + 0x590e210) = f;
    wchar_t text[16]; swprintf_s(text, L"%u", static_cast<unsigned>(f));
    if (!WritePrivateProfileStringW(L"Campaign", L"FieldOfView", text, g_ini)) Log("could not save the field of view (%lu)", GetLastError());
}
int RegisterFov()
{
    auto manager = *reinterpret_cast<uint64_t*>(g_base + 0x6222428); auto typeId = *reinterpret_cast<uint32_t*>(g_base + 0x6417b24);
    if (!manager || typeId != 74) return 10;
    auto type = *reinterpret_cast<uint64_t*>(*reinterpret_cast<uint64_t*>(manager + 0x48) + typeId * 8);
    if (!type || strcmp(*reinterpret_cast<char**>(type + 0x60), "PlayerProfile")) return 11;
    auto begin = *reinterpret_cast<uint64_t**>(type + 8), end = *reinterpret_cast<uint64_t**>(type + 16); if (end < begin || end - begin > 512) return 12;
    for (auto p = begin; p != end; ++p) if (!strcmp(*reinterpret_cast<char**>(*p + 8), "CampaignFieldOfView")) return 13;
    auto property = reinterpret_cast<uint64_t(*)(size_t)>(g_base + 0x1c8b430)(0x50); if (!property) return 14;
    reinterpret_cast<void(*)(uint64_t, const char*, int, int)>(g_base + 0x1e11050)(property, "CampaignFieldOfView", 1, 3);
    *reinterpret_cast<uint64_t*>(property) = g_base + 0x3842b50;
    *reinterpret_cast<void**>(property + 0x40) = reinterpret_cast<void*>(&GetCampaignFov); *reinterpret_cast<void**>(property + 0x48) = reinterpret_cast<void*>(&SetCampaignFov);
    reinterpret_cast<void(*)(uint64_t, uint64_t)>(g_base + 0x1f32f50)(type, property);
    *reinterpret_cast<float*>(g_base + 0x590e210) = g_fov;
    return 0;
}
// halo5onpc's update-thread entry (+0x869460): the first calls after the PlayerProfile type exists register the property.
using UpdateFn = uint64_t(*)();   // takes no arguments (uses only its own shadow space)
UpdateFn g_update = nullptr;
uint64_t Update()
{
    if (!g_registered)
    {
        int rc = 0;
        __try { rc = RegisterFov(); } __except (EXCEPTION_EXECUTE_HANDLER) { rc = -1; }
        if (rc == 0 || rc == 13 || rc == -1 || rc == 12 || rc == 14)
        {
            InterlockedExchange(&g_registered, 1);
            if (rc == 0) Log("campaign field of view registered (%.0f)", g_fov); else Log("field of view not registered (code %d)", rc);
        }
    }
    return g_update();
}

// ---- frame rate traps -------------------------------------------------------------------------------
LONG CALLBACK Trap(EXCEPTION_POINTERS* x)
{
    if (x->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    auto a = reinterpret_cast<uintptr_t>(x->ExceptionRecord->ExceptionAddress); auto c = x->ContextRecord;
    if (a == g_base + 0x1551ecf)   // frame pacing: period for the selected option
    {
        int option = static_cast<int>(c->R14);
        if (option == 3 || option == 4) { float period = 1.0f / (option == 3 ? 144.0f : 180.0f); memcpy(&c->Xmm10.Low, &period, 4); InterlockedIncrement(&g_paceHits); }
        c->Rdi = static_cast<uint64_t>(static_cast<int64_t>(*reinterpret_cast<int32_t*>(c->Rcx + 8))); c->Rip = a + 4; return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (a == g_base + 0x152b28e)   // present interval: 144/180 present every vblank
    {
        uint32_t interval = static_cast<uint32_t>(c->Rbx); if (interval == 3 || interval == 4) interval = 1;
        c->Rdx = interval; c->Rip = a + 2; return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (a == g_base + 0x155f483)   // original cmp/jg treats every option above 1 as 30 FPS
    {
        int option = *reinterpret_cast<int*>(c->Rcx + 0xac);
        if (option == 3 || option == 4) { *reinterpret_cast<unsigned char*>(g_base + 0x614168a) = 0; c->Rip = g_base + 0x155f4f1; }
        else c->Rip = g_base + (option > 1 ? 0x155f4ea : 0x155f48c);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (a == g_base + 0xad5217)   // metronome: only its normal 60 Hz period changes (cinematic 30 Hz modes keep theirs)
    {
        int option = *reinterpret_cast<int*>(g_base + 0x614194c);
        if ((option == 3 || option == 4) && c->Rdi == 16666) { c->Rdi = option == 3 ? 6944 : 5556; InterlockedIncrement(&g_clockHits); }
        c->Rcx = __readgsqword(0x58); c->Rip = a + 9; return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
}

void InstallDisplayOptions()
{
    wchar_t v[4]{}; if (GetEnvironmentVariableW(L"FORGE_DISPLAY_OPTIONS", v, 4) && v[0] == L'0') return;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(g_base + reinterpret_cast<IMAGE_DOS_HEADER*>(g_base)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != 1520325666) { Log("unsupported halo5forge.exe build; not applied"); return; }
    static const unsigned char pace[] = { 0x48, 0x63, 0x79, 0x08 }, present[] = { 0x8b, 0xd3 }, limit[] = { 0x83, 0xb9, 0xac, 0x00, 0x00, 0x00, 0x01, 0x7f },
        clock[] = { 0x65, 0x48, 0x8b, 0x0c, 0x25, 0x58, 0x00, 0x00, 0x00 }, update[] = { 0x41, 0x56, 0x48, 0x83, 0xec, 0x40 },
        alloc[] = { 0xba, 0x04, 0x00, 0x00, 0x00, 0x48, 0xff, 0x25 }, init[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20 }, add[] = { 0x48, 0x89, 0x5c, 0x24, 0x08 };
    if (!Bytes(0x1551ecf, pace, sizeof(pace)) || !Bytes(0x152b28e, present, sizeof(present)) || !Bytes(0x155f483, limit, sizeof(limit)) || !Bytes(0xad5217, clock, sizeof(clock)) ||
        !Bytes(0x869460, update, sizeof(update)) || !Bytes(0x1c8b430, alloc, sizeof(alloc)) || !Bytes(0x1e11050, init, sizeof(init)) || !Bytes(0x1f32f50, add, sizeof(add)))
    { Log("code differs from the supported build; not applied"); return; }
    GetCurrentDirectoryW(MAX_PATH, g_ini); wcscat_s(g_ini, L"\\h5campaign\\display.ini");
    int fov = GetPrivateProfileIntW(L"Campaign", L"FieldOfView", 78, g_ini); g_fov = static_cast<float>(fov >= 60 && fov <= 120 ? fov : 78);
    if (!AddVectoredExceptionHandler(1, Trap)) { Log("exception handler unavailable; not applied"); return; }
    const uintptr_t points[] = { 0x1551ecf, 0x152b28e, 0x155f483, 0xad5217 }; const unsigned char original[] = { 0x48, 0x8b, 0x83, 0x65 };
    for (int i = 0; i < 4; ++i) if (!Swap(points[i], original[i], 0xcc)) { for (int j = i; j > 0; --j) Swap(points[j - 1], 0xcc, original[j - 1]); Log("could not place the frame rate traps"); return; }
    MH_STATUS st = MH_Initialize();
    void* target = reinterpret_cast<void*>(g_base + 0x869460);
    if ((st == MH_OK || st == MH_ERROR_ALREADY_INITIALIZED) && MH_CreateHook(target, reinterpret_cast<void*>(&Update), reinterpret_cast<void**>(&g_update)) == MH_OK && MH_EnableHook(target) == MH_OK)
        Log("144/180 FPS options and campaign field of view installed (fov %.0f, %ls)", g_fov, g_ini);
    else Log("frame rate options installed; field of view hook failed");
}
