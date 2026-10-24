// FORGE_TRACE_FILES=1: log which game data files Forge opens (and which it fails to find), to see whether
// baked content (mapinfo, modules, __cms__ campaign variants, audio banks) is read from the files at all.
// Logs each distinct path once (plus every failure, up to a limit). Paths only; no file contents.
#include <windows.h>
#include <string>
#include <set>
#include <mutex>
#include <cwctype>
#include <cstdio>
#include <cstring>
#include "../third_party/minhook/include/MinHook.h"

namespace {
using Open2 = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, DWORD, LPCREATEFILE2_EXTENDED_PARAMETERS);
using OpenW = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using AttrEx = BOOL(WINAPI*)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
Open2 g_open2 = nullptr; OpenW g_openW = nullptr; AttrEx g_attr = nullptr;
std::mutex g_lock; std::set<std::wstring> g_seen; long g_failures = 0;
bool Interesting(const std::wstring& p) {
    std::wstring l = p; for (auto& c : l) c = static_cast<wchar_t>(towlower(c));
    return l.find(L"__cms__") != std::wstring::npos || l.find(L".module") != std::wstring::npos || l.find(L".mapinfo") != std::wstring::npos ||
           l.find(L".pck") != std::wstring::npos || l.find(L"\\sound\\") != std::wstring::npos || l.find(L"campaign") != std::wstring::npos;
}
void Note(const wchar_t* kind, LPCWSTR path, bool ok, DWORD error) {
    if (!path || !Interesting(path)) return;
    std::wstring key = std::wstring(kind) + (ok ? L"+" : L"-") + path;
    { std::lock_guard<std::mutex> l(g_lock); if (!g_seen.insert(key).second) return; if (!ok && ++g_failures > 400) return; }
    wchar_t line[1200]; swprintf_s(line, L"[ForgeShim] FILE %s %s %s (err %lu)\n", kind, ok ? L"ok  " : L"FAIL", path, ok ? 0ul : error);
    OutputDebugStringW(line);
}
HANDLE WINAPI Hook2(LPCWSTR p, DWORD a, DWORD s, DWORD d, LPCREATEFILE2_EXTENDED_PARAMETERS x) {
    HANDLE h = g_open2(p, a, s, d, x); DWORD e = GetLastError(); Note(L"open ", p, h != INVALID_HANDLE_VALUE, e); SetLastError(e); return h;
}
HANDLE WINAPI HookW(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD d, DWORD f, HANDLE t) {
    HANDLE h = g_openW(p, a, s, sa, d, f, t); DWORD e = GetLastError(); Note(L"open ", p, h != INVALID_HANDLE_VALUE, e); SetLastError(e); return h;
}
BOOL WINAPI HookAttr(LPCWSTR p, GET_FILEEX_INFO_LEVELS l, LPVOID o) {
    BOOL r = g_attr(p, l, o); DWORD e = GetLastError(); Note(L"stat ", p, r != FALSE, e); SetLastError(e); return r;
}
// Who ends the process (exit code 1 without a crash record = a deliberate exit after a fatal error).
using ExitFn = void(WINAPI*)(UINT); using TermFn = BOOL(WINAPI*)(HANDLE, UINT);
ExitFn g_exit = nullptr; TermFn g_term = nullptr;
void Stack(const char* what, UINT code) {
    void* frames[32]{}; USHORT got = RtlCaptureStackBackTrace(1, 32, frames, nullptr);
    char line[2400]; int n = sprintf_s(line, "[ForgeShim] EXIT %s code=%u stack:", what, code);
    for (USHORT i = 0; i < got && n < 2200; ++i) {
        HMODULE m = nullptr; char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(frames[i]), &m) && m) {
            GetModuleFileNameA(m, name, MAX_PATH); }
        const char* leaf = strrchr(name, '\\'); leaf = leaf ? leaf + 1 : name;
        n += sprintf_s(line + n, sizeof(line) - n, " %s+%llx", leaf, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(m)));
    }
    strcat_s(line, "\n"); OutputDebugStringA(line);
}
void WINAPI HookExit(UINT code) { Stack("ExitProcess", code); Sleep(200); g_exit(code); }
BOOL WINAPI HookTerm(HANDLE process, UINT code) {
    if (process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId()) { Stack("TerminateProcess", code); Sleep(200); }
    return g_term(process, code);
}
}

void InstallFileTrace() {
    if (!GetEnvironmentVariableW(L"FORGE_TRACE_FILES", nullptr, 0)) return;
    MH_STATUS st = MH_Initialize(); if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return;
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll"); if (!kb) return;
    int ok = 0;
    if (auto f = GetProcAddress(kb, "CreateFile2")) ok += MH_CreateHook(reinterpret_cast<void*>(f), reinterpret_cast<void*>(&Hook2), reinterpret_cast<void**>(&g_open2)) == MH_OK && MH_EnableHook(reinterpret_cast<void*>(f)) == MH_OK;
    if (auto f = GetProcAddress(kb, "CreateFileW")) ok += MH_CreateHook(reinterpret_cast<void*>(f), reinterpret_cast<void*>(&HookW), reinterpret_cast<void**>(&g_openW)) == MH_OK && MH_EnableHook(reinterpret_cast<void*>(f)) == MH_OK;
    if (auto f = GetProcAddress(kb, "GetFileAttributesExW")) ok += MH_CreateHook(reinterpret_cast<void*>(f), reinterpret_cast<void*>(&HookAttr), reinterpret_cast<void**>(&g_attr)) == MH_OK && MH_EnableHook(reinterpret_cast<void*>(f)) == MH_OK;
    if (auto f = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitProcess")) { MH_CreateHook(reinterpret_cast<void*>(f), reinterpret_cast<void*>(&HookExit), reinterpret_cast<void**>(&g_exit)); MH_EnableHook(reinterpret_cast<void*>(f)); }
    if (auto f = GetProcAddress(kb, "TerminateProcess")) { MH_CreateHook(reinterpret_cast<void*>(f), reinterpret_cast<void*>(&HookTerm), reinterpret_cast<void**>(&g_term)); MH_EnableHook(reinterpret_cast<void*>(f)); }
    OutputDebugStringA(ok == 3 ? "[ForgeShim] FILE tracing data file opens\n" : "[ForgeShim] FILE trace hooks incomplete\n");
}
