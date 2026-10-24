// Unattended test runs: FORGE_MUTE=1 mutes this process's audio sessions (so an automated run is silent);
// the process exits when <Forge root>\h5campaign\quit.flag appears (so a run can be stopped without the user).
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <wrl/client.h>
#include <thread>
#include <cstdio>
#include "../third_party/minhook/include/MinHook.h"
using Microsoft::WRL::ComPtr;

namespace {
// Exit tracing (always on): the game leaves through ExitProcess/TerminateProcess when its own fatal-error path runs
// (a crash that ends with exit code 1 and no exception). Log the code and the caller chain as halo5forge offsets.
using ExitFn = void(WINAPI*)(UINT); using TermFn = BOOL(WINAPI*)(HANDLE, UINT);
ExitFn g_exitProcess = nullptr; TermFn g_terminate = nullptr;
void LogExit(const char* how, UINT code) {
    static volatile LONG once = 0; if (InterlockedExchange(&once, 1)) return;
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    void* frames[48]{}; USHORT got = RtlCaptureStackBackTrace(2, 48, frames, nullptr); char line[1200]; int n = sprintf_s(line, "[ForgeShim] EXIT %s code=%u thread=%lu from", how, code, GetCurrentThreadId());
    for (USHORT i = 0; i < got && n < 1100; ++i) {
        auto a = reinterpret_cast<uintptr_t>(frames[i]); HMODULE m = nullptr; char name[64] = "?";
        if (a >= base && a < base + 0x7000000) { n += sprintf_s(line + n, sizeof(line) - n, " +%llx", static_cast<unsigned long long>(a - base)); continue; }
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(a), &m) && m) {
            char path[MAX_PATH]; GetModuleFileNameA(m, path, MAX_PATH); const char* f = strrchr(path, '\\'); strncpy_s(name, f ? f + 1 : path, _TRUNCATE);
        }
        n += sprintf_s(line + n, sizeof(line) - n, " %s+%llx", name, static_cast<unsigned long long>(a - reinterpret_cast<uintptr_t>(m)));
    }
    strcat_s(line, "\n"); OutputDebugStringA(line); Sleep(200);
}
void WINAPI HookExitProcess(UINT code) { LogExit("ExitProcess", code); g_exitProcess(code); }
BOOL WINAPI HookTerminate(HANDLE h, UINT code) { if (h == GetCurrentProcess() || GetProcessId(h) == GetCurrentProcessId()) LogExit("TerminateProcess", code); return g_terminate(h, code); }
void InstallExitTrace() {
    MH_Initialize();
    if (auto f = GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "TerminateProcess")) { MH_CreateHook(reinterpret_cast<void*>(f), reinterpret_cast<void*>(&HookTerminate), reinterpret_cast<void**>(&g_terminate)); MH_EnableHook(reinterpret_cast<void*>(f)); }
    if (auto f = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitProcess")) {
        if (MH_CreateHook(reinterpret_cast<void*>(f), reinterpret_cast<void*>(&HookExitProcess), reinterpret_cast<void**>(&g_exitProcess)) == MH_OK) MH_EnableHook(reinterpret_cast<void*>(f));
    }
}
// FORGE_LOG_FILE=<path>: every OutputDebugString line (shim and game) also goes to this file, so logs exist without
// a debug-output listener (the player launcher on the PS5). Plain WriteFile appends: nothing is lost if the game dies.
HANDLE g_logFile = INVALID_HANDLE_VALUE; LONGLONG g_logBytes = 0; constexpr LONGLONG kLogCap = 256ll << 20;
CRITICAL_SECTION g_logLock;
using OdsAFn = void(WINAPI*)(LPCSTR); using OdsWFn = void(WINAPI*)(LPCWSTR);
OdsAFn g_odsA = nullptr; OdsWFn g_odsW = nullptr;
thread_local bool t_inLog = false;
void LogWrite(const char* s, size_t n) {
    if (g_logFile == INVALID_HANDLE_VALUE || !n) return;
    EnterCriticalSection(&g_logLock);
    if (g_logBytes < kLogCap) { DWORD put = 0; WriteFile(g_logFile, s, static_cast<DWORD>(n), &put, nullptr); g_logBytes += put;
        if (g_logBytes >= kLogCap) { const char m[] = "\n[ForgeShim] log size cap reached; further lines dropped\n"; WriteFile(g_logFile, m, sizeof(m) - 1, &put, nullptr); } }
    LeaveCriticalSection(&g_logLock);
}
void WINAPI HookOdsA(LPCSTR s) { if (s && !t_inLog) { t_inLog = true; LogWrite(s, strlen(s)); t_inLog = false; } g_odsA(s); }
void WINAPI HookOdsW(LPCWSTR s) {
    if (s && !t_inLog) { t_inLog = true; char b[2048]; int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, b, sizeof(b), nullptr, nullptr); if (n > 1) LogWrite(b, n - 1); t_inLog = false; }
    bool was = t_inLog; t_inLog = true; g_odsW(s); t_inLog = was;   // kernelbase's W converts and calls A: don't log twice
}
}
void InstallLogFile() {
    wchar_t path[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"FORGE_LOG_FILE", path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    g_logFile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_logFile == INVALID_HANDLE_VALUE) return;
    InitializeCriticalSection(&g_logLock);
    MH_STATUS st = MH_Initialize(); if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return;
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll"); if (!kb) kb = GetModuleHandleW(L"kernel32.dll");
    if (auto f = GetProcAddress(kb, "OutputDebugStringA")) { if (MH_CreateHook(reinterpret_cast<void*>(f), reinterpret_cast<void*>(&HookOdsA), reinterpret_cast<void**>(&g_odsA)) == MH_OK) MH_EnableHook(reinterpret_cast<void*>(f)); }
    if (auto f = GetProcAddress(kb, "OutputDebugStringW")) { if (MH_CreateHook(reinterpret_cast<void*>(f), reinterpret_cast<void*>(&HookOdsW), reinterpret_cast<void**>(&g_odsW)) == MH_OK) MH_EnableHook(reinterpret_cast<void*>(f)); }
    OutputDebugStringA("[ForgeShim] log file opened\n");
}
namespace {
int MuteOwnSessions() {
    ComPtr<IMMDeviceEnumerator> en; if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return -1;
    ComPtr<IMMDeviceCollection> devices; if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) return -1;
    UINT count = 0; devices->GetCount(&count); int muted = 0;
    for (UINT d = 0; d < count; ++d) {
        ComPtr<IMMDevice> dev; if (FAILED(devices->Item(d, &dev))) continue;
        ComPtr<IAudioSessionManager2> mgr; if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(mgr.GetAddressOf())))) continue;
        ComPtr<IAudioSessionEnumerator> sessions; if (FAILED(mgr->GetSessionEnumerator(&sessions))) continue;
        int n = 0; sessions->GetCount(&n);
        for (int i = 0; i < n; ++i) {
            ComPtr<IAudioSessionControl> c; if (FAILED(sessions->GetSession(i, &c))) continue;
            ComPtr<IAudioSessionControl2> c2; if (FAILED(c.As(&c2))) continue;
            DWORD pid = 0; c2->GetProcessId(&pid); if (pid != GetCurrentProcessId()) continue;
            ComPtr<ISimpleAudioVolume> vol; if (SUCCEEDED(c.As(&vol))) { BOOL m = FALSE; vol->GetMute(&m); if (!m) { vol->SetMute(TRUE, nullptr); ++muted; } }
        }
    }
    return muted;
}
}

void InstallAutomation() {
    InstallExitTrace();
    bool mute = GetEnvironmentVariableW(L"FORGE_MUTE", nullptr, 0) != 0;
    std::thread([mute]() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        wchar_t flag[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, flag); wcscat_s(flag, L"\\h5campaign\\quit.flag");
        DeleteFileW(flag);
        for (unsigned tick = 0;; ++tick) {
            Sleep(500);
            if (mute && tick % 4 == 0) { int m = MuteOwnSessions(); if (m > 0) { char l[80]; sprintf_s(l, "[ForgeShim] AUTO muted %d audio session(s)\n", m); OutputDebugStringA(l); } }
            if (GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES) { OutputDebugStringA("[ForgeShim] AUTO quit.flag found: exiting\n"); Sleep(300); TerminateProcess(GetCurrentProcess(), 0); }
        }
    }).detach();
}
