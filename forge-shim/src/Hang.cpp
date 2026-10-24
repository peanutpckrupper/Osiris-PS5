// Hang watchdog (always on): counts the game's presented frames. When none has been presented for 8 s it logs every
// thread's CPU time (no thread is touched); at 30 s (a real freeze, not a loading hitch) it logs the CPU times again
// and every thread's call stack as "HANG" lines, so a freeze leaves the place it is stuck in. Reading a stack briefly
// suspends that thread; FORGE_HANG_WATCH=times keeps it to CPU times only.
// The walk uses each module's own unwind table (snapshotted before any thread is suspended) and RtlVirtualUnwind,
// which takes no locks, so a thread suspended while holding the loader or heap lock can't deadlock the dump.
// FORGE_HANG_WATCH=0 turns it off.
#include <windows.h>
#include <dxgi1_2.h>
#include <d3d12.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../third_party/minhook/include/MinHook.h"
#pragma comment(lib, "kernel32.lib")

namespace {
volatile LONG64 g_frames = 0; volatile LONG64 g_lastFrame = 0;   // GetTickCount64 of the last present
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
PresentFn g_present = nullptr; Present1Fn g_present1 = nullptr;
ID3D12Device* g_device = nullptr;   // the swap chain's device (kept referenced), for its removed reason in a dump
void Tick() { InterlockedIncrement64(&g_frames); InterlockedExchange64(&g_lastFrame, static_cast<LONG64>(GetTickCount64())); }
HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* s, UINT i, UINT f) { HRESULT hr = g_present(s, i, f); Tick(); return hr; }
HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1* s, UINT i, UINT f, const DXGI_PRESENT_PARAMETERS* p) { HRESULT hr = g_present1(s, i, f, p); Tick(); return hr; }

struct Module { DWORD64 base, end; const RUNTIME_FUNCTION* table; DWORD count; char name[48]; };
std::vector<Module> g_modules;
void SnapshotModules()
{
    g_modules.clear(); HMODULE mods[512]; DWORD need = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need)) return;
    for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 512; ++i)
    {
        auto b = reinterpret_cast<BYTE*>(mods[i]); auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(b);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) continue; auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(b + dos->e_lfanew);
        if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) continue;
        auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        Module m{ reinterpret_cast<DWORD64>(b), reinterpret_cast<DWORD64>(b) + nt->OptionalHeader.SizeOfImage,
                  dir.VirtualAddress ? reinterpret_cast<const RUNTIME_FUNCTION*>(b + dir.VirtualAddress) : nullptr,
                  dir.Size / static_cast<DWORD>(sizeof(RUNTIME_FUNCTION)), "?" };
        char path[MAX_PATH]; if (GetModuleFileNameA(mods[i], path, MAX_PATH)) { const char* f = strrchr(path, '\\'); strncpy_s(m.name, f ? f + 1 : path, _TRUNCATE); }
        g_modules.push_back(m);
    }
}
const Module* ModuleOf(DWORD64 pc) { for (auto& m : g_modules) if (pc >= m.base && pc < m.end) return &m; return nullptr; }
const RUNTIME_FUNCTION* Lookup(DWORD64 pc, DWORD64* imageBase)
{
    const Module* m = ModuleOf(pc); if (!m || !m->table) return nullptr; *imageBase = m->base;
    DWORD rva = static_cast<DWORD>(pc - m->base); DWORD lo = 0, hi = m->count;
    while (lo < hi) { DWORD mid = (lo + hi) / 2; auto& f = m->table[mid]; if (rva < f.BeginAddress) hi = mid; else if (rva >= f.EndAddress) lo = mid + 1; else return &f; }
    return nullptr;
}
constexpr int kDepth = 40;
// Walks a suspended thread's stack. No locks, no allocation; faults while reading a broken stack just end the walk.
int Walk(CONTEXT c, DWORD64* pcs)
{
    int n = 0;
    __try
    {
        for (; n < kDepth && c.Rip; ++n)
        {
            pcs[n] = c.Rip; DWORD64 imageBase = 0; auto fe = Lookup(c.Rip, &imageBase);
            if (!fe) { if (n) { ++n; break; } c.Rip = *reinterpret_cast<DWORD64*>(c.Rsp); c.Rsp += 8; continue; }
            void* handlerData = nullptr; DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, const_cast<PRUNTIME_FUNCTION>(fe), &c, &handlerData, &frame, nullptr);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}
struct Stack { DWORD tid; int n; DWORD64 pcs[kDepth]; wchar_t desc[48]; DWORD suspendErr; ULONGLONG cpuMs; };
using DescFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
void Dump(DWORD stalledMs, bool walk)
{
    SnapshotModules();
    std::vector<DWORD> tids; HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) { THREADENTRY32 te{ sizeof(te) }; for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) if (te.th32OwnerProcessID == GetCurrentProcessId() && te.th32ThreadID != GetCurrentThreadId()) tids.push_back(te.th32ThreadID); CloseHandle(snap); }
    std::vector<Stack> stacks(tids.size());
    static auto describe = reinterpret_cast<DescFn>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "GetThreadDescription"));
    for (size_t i = 0; i < tids.size(); ++i)
    {
        Stack& s = stacks[i]; s.tid = tids[i]; s.n = 0; s.desc[0] = 0; s.suspendErr = 0;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, s.tid); if (!h) { s.suspendErr = GetLastError(); continue; }
        PWSTR d = nullptr; if (describe && SUCCEEDED(describe(h, &d)) && d) { wcsncpy_s(s.desc, d, _TRUNCATE); LocalFree(d); }
        FILETIME c0, e0, k, u; s.cpuMs = 0;
        if (GetThreadTimes(h, &c0, &e0, &k, &u)) s.cpuMs = ((static_cast<ULONGLONG>(k.dwHighDateTime) << 32 | k.dwLowDateTime) + (static_cast<ULONGLONG>(u.dwHighDateTime) << 32 | u.dwLowDateTime)) / 10000;
        if (!walk) { CloseHandle(h); continue; }
        if (SuspendThread(h) == static_cast<DWORD>(-1)) { s.suspendErr = GetLastError(); CloseHandle(h); continue; }
        CONTEXT c{}; c.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(h, &c)) s.n = Walk(c, s.pcs);
        ResumeThread(h); CloseHandle(h);
    }
    HRESULT removed = g_device ? g_device->GetDeviceRemovedReason() : E_POINTER;
    char head[220]; sprintf_s(head, "[ForgeShim] HANG no frame presented for %lu s (%lld frames before); D3D12 device removed reason %08lX%s; %zu threads:\n", stalledMs / 1000, g_frames, static_cast<unsigned long>(removed), removed == S_OK ? " (device still alive)" : "", stacks.size()); OutputDebugStringA(head);
    auto exeBase = reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr));
    for (auto& s : stacks)
    {
        char line[2000]; int n = sprintf_s(line, "[ForgeShim] HANG t=%lu '%ls' cpu=%llums", s.tid, s.desc, s.cpuMs);
        if (s.suspendErr) n += sprintf_s(line + n, sizeof(line) - n, " (not walked: %lu)", s.suspendErr);
        for (int i = 0; i < s.n && n < 1900; ++i)
        {
            const Module* m = ModuleOf(s.pcs[i]);
            if (m && m->base == exeBase) n += sprintf_s(line + n, sizeof(line) - n, " +%llx", s.pcs[i] - m->base);
            else if (m) n += sprintf_s(line + n, sizeof(line) - n, " %s+%llx", m->name, s.pcs[i] - m->base);
            else n += sprintf_s(line + n, sizeof(line) - n, " %llx", s.pcs[i]);
        }
        strcat_s(line, "\n"); OutputDebugStringA(line);
    }
}
bool Enabled() { wchar_t v[8]; DWORD n = GetEnvironmentVariableW(L"FORGE_HANG_WATCH", v, 8); return !(n && v[0] == L'0'); }
bool Walking() { wchar_t v[8]; DWORD n = GetEnvironmentVariableW(L"FORGE_HANG_WATCH", v, 8); return !(n && _wcsicmp(v, L"times") == 0); }
DWORD WINAPI Watch(void*)
{
    LONG64 seen = -1; int dumps = 0;
    for (;;)
    {
        Sleep(1000);
        LONG64 frames = g_frames; if (!frames) continue;
        if (frames != seen) { if (dumps) { char l[120]; sprintf_s(l, "[ForgeShim] HANG over: frames resumed (%lld)\n", frames); OutputDebugStringA(l); } seen = frames; dumps = 0; continue; }
        DWORD stalled = static_cast<DWORD>(GetTickCount64() - static_cast<ULONGLONG>(g_lastFrame));
        if (dumps == 0 && stalled >= 8000) { Dump(stalled, false); ++dumps; }
        else if (dumps == 1 && stalled >= 30000) { Dump(stalled, Walking()); ++dumps; }
    }
}
}

struct ComPtr12 { ID3D12CommandQueue* p = nullptr; ~ComPtr12() { if (p) p->Release(); } };
// Called with each swap chain the game creates (Dxgi.cpp).
void HangWatchSwapChain(IDXGISwapChain1* swap)
{
    static volatile LONG once = 0; if (!swap || !Enabled() || InterlockedExchange(&once, 1)) return;
    MH_Initialize();
    if (FAILED(swap->GetDevice(IID_PPV_ARGS(&g_device))) || !g_device)
    { g_device = nullptr; ComPtr12 queue; if (SUCCEEDED(swap->GetDevice(__uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue.p))) && queue.p) queue.p->GetDevice(IID_PPV_ARGS(&g_device)); }
    void** vt = *reinterpret_cast<void***>(swap);
    bool ok = MH_CreateHook(vt[8], reinterpret_cast<void*>(&HookPresent), reinterpret_cast<void**>(&g_present)) == MH_OK && MH_EnableHook(vt[8]) == MH_OK;
    bool ok1 = MH_CreateHook(vt[22], reinterpret_cast<void*>(&HookPresent1), reinterpret_cast<void**>(&g_present1)) == MH_OK && MH_EnableHook(vt[22]) == MH_OK;
    if (HANDLE t = CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr)) CloseHandle(t);
    char l[120]; sprintf_s(l, "[ForgeShim] HANG watchdog on (Present %s, Present1 %s)\n", ok ? "hooked" : "not hooked", ok1 ? "hooked" : "not hooked"); OutputDebugStringA(l);
}
