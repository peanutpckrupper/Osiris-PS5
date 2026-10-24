// ForgeLauncher: starts halo5forge.exe as a normal desktop process (no debugger), injects
// ForgeDesktopShim.dll at the exe entry point and calls its InitializeShim export.
// Usage: ForgeLauncher.exe <forge-folder> <shim-dll> [--log <file>] [--seconds N]
//   --log      capture OutputDebugString from the game (DBWIN channel, no debugger) into <file>
//   --seconds  terminate the game after N seconds (default 0 = never; launcher waits for exit)
#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include <atomic>
#include <thread>

static void Log(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); printf("\n"); fflush(stdout); }

static uintptr_t EntryRva(const std::wstring& exe) {
    HANDLE f = CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 0;
    BYTE hdr[4096]{}; DWORD got = 0; ReadFile(f, hdr, sizeof(hdr), &got, nullptr); CloseHandle(f);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hdr); if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew > 3800) return 0;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(hdr + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt->OptionalHeader.AddressOfEntryPoint : 0;
}

static bool RunRemote(HANDLE proc, void* fn, void* arg, DWORD& exitCode, DWORD timeoutMs) {
    HANDLE t = CreateRemoteThread(proc, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(fn), arg, 0, nullptr);
    if (!t) return false;
    bool ok = WaitForSingleObject(t, timeoutMs) == WAIT_OBJECT_0;
    if (ok) GetExitCodeThread(t, &exitCode);
    CloseHandle(t); return ok;
}

// DBWIN capture (the DebugView mechanism): works whenever no debugger is attached.
static std::atomic<bool> g_stop{false};
static void CaptureDebugOutput(DWORD pid, std::wstring path) {
    HANDLE bufReady = CreateEventW(nullptr, FALSE, FALSE, L"DBWIN_BUFFER_READY");
    HANDLE dataReady = CreateEventW(nullptr, FALSE, FALSE, L"DBWIN_DATA_READY");
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, L"DBWIN_BUFFER");
    if (!bufReady || !dataReady || !map) { Log("debug capture unavailable (err=%lu)", GetLastError()); return; }
    if (GetLastError() == ERROR_ALREADY_EXISTS) Log("note: another debug-output listener (e.g. DebugView) may be running");
    auto view = static_cast<BYTE*>(MapViewOfFile(map, FILE_MAP_READ, 0, 0, 4096));
    FILE* out = nullptr; _wfopen_s(&out, path.c_str(), L"wb");
    if (!view || !out) { Log("debug capture open failed"); return; }
    SetEvent(bufReady);
    while (!g_stop) {
        if (WaitForSingleObject(dataReady, 250) != WAIT_OBJECT_0) continue;
        DWORD from = *reinterpret_cast<DWORD*>(view);
        if (from == pid) { const char* s = reinterpret_cast<const char*>(view + 4); fwrite(s, 1, strnlen(s, 4092), out); fflush(out); }
        SetEvent(bufReady);
    }
    fclose(out); UnmapViewOfFile(view); CloseHandle(map); CloseHandle(dataReady); CloseHandle(bufReady);
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) { fwprintf(stderr, L"Usage: ForgeLauncher.exe <forge-folder> <shim-dll> [--log <file>] [--seconds N]\n"); return 2; }
    std::wstring root = argv[1], shim = argv[2], logPath; DWORD seconds = 0;
    for (int i = 3; i < argc; ++i) {
        if (!_wcsicmp(argv[i], L"--log") && i + 1 < argc) logPath = argv[++i];
        else if (!_wcsicmp(argv[i], L"--seconds") && i + 1 < argc) seconds = _wtoi(argv[++i]);
    }
    wchar_t full[MAX_PATH]; GetFullPathNameW(shim.c_str(), MAX_PATH, full, nullptr); shim = full;
    GetFullPathNameW(root.c_str(), MAX_PATH, full, nullptr); root = full;
    if (!_wcsicmp(root.c_str(), L"C:\\Games\\H5Forge")) { Log("refusing to run from the read-only original folder"); return 2; }
    std::wstring exe = root + L"\\halo5forge.exe", cmd = L"\"" + exe + L"\"";
    uintptr_t entryRva = EntryRva(exe); if (!entryRva) { Log("cannot read entry point"); return 3; }

    STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, root.c_str(), &si, &pi)) { Log("CreateProcess error=%lu", GetLastError()); return 3; }
    Log("child pid=%lu", pi.dwProcessId);
    std::thread capture;
    if (!logPath.empty()) capture = std::thread(CaptureDebugOutput, pi.dwProcessId, logPath);
    auto fail = [&](const char* why) { Log("%s (err=%lu)", why, GetLastError()); TerminateProcess(pi.hProcess, 1); g_stop = true; if (capture.joinable()) capture.join(); return 4; };

    // Image base from PEB (Rdx of the initial thread points at the PEB).
    CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_FULL; if (!GetThreadContext(pi.hThread, &ctx)) return fail("GetThreadContext");
    uintptr_t base = 0; SIZE_T n = 0;
    if (!ReadProcessMemory(pi.hProcess, reinterpret_cast<void*>(ctx.Rdx + 0x10), &base, sizeof(base), &n)) return fail("read PEB");
    auto entry = reinterpret_cast<BYTE*>(base + entryRva);
    BYTE orig[2]{}, loop[2]{ 0xEB, 0xFE }; DWORD old = 0;
    if (!ReadProcessMemory(pi.hProcess, entry, orig, 2, &n) || !VirtualProtectEx(pi.hProcess, entry, 2, PAGE_EXECUTE_READWRITE, &old) ||
        !WriteProcessMemory(pi.hProcess, entry, loop, 2, &n)) return fail("patch entry");
    FlushInstructionCache(pi.hProcess, entry, 2);
    ResumeThread(pi.hThread);
    // Wait for the loader to finish (static imports, TLS) and the main thread to spin at the entry point.
    bool reached = false;
    for (int i = 0; i < 6000 && !reached; ++i) {
        Sleep(5); SuspendThread(pi.hThread); ctx.ContextFlags = CONTEXT_CONTROL; GetThreadContext(pi.hThread, &ctx);
        reached = ctx.Rip == reinterpret_cast<uintptr_t>(entry); if (!reached) ResumeThread(pi.hThread);
        DWORD code = 0; if (GetExitCodeProcess(pi.hProcess, &code) && code != STILL_ACTIVE) { Log("game exited during load, code=%08lX", code); return fail("early exit"); }
    }
    if (!reached) return fail("entry point not reached in 30 s");
    WriteProcessMemory(pi.hProcess, entry, orig, 2, &n); VirtualProtectEx(pi.hProcess, entry, 2, old, &old); FlushInstructionCache(pi.hProcess, entry, 2);
    Log("loader done; injecting %ls", shim.c_str());

    // kernel32 is mapped at the same address in every process of this boot session.
    void* loadLib = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    size_t bytes = (shim.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(pi.hProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD rc = 0;
    if (!remote || !WriteProcessMemory(pi.hProcess, remote, shim.c_str(), bytes, &n)) return fail("write shim path");
    if (!RunRemote(pi.hProcess, loadLib, remote, rc, 30000) || !rc) return fail("LoadLibraryW in game failed");
    VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);
    // Remote module base: LoadLibraryW's 32-bit exit code is the low half; resolve fully via a local mapping.
    HMODULE local = LoadLibraryExW(shim.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    auto init = local ? reinterpret_cast<uintptr_t>(GetProcAddress(local, "InitializeShim")) : 0;
    if (!init) return fail("InitializeShim export missing");
    uintptr_t remoteBase = 0;
    {   // find the shim's base in the game by scanning modules
        HMODULE mods[1024]; DWORD need = 0;
        typedef BOOL(WINAPI* EnumMods)(HANDLE, HMODULE*, DWORD, LPDWORD, DWORD);
        typedef DWORD(WINAPI* ModName)(HANDLE, HMODULE, LPWSTR, DWORD);
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        auto enumMods = reinterpret_cast<EnumMods>(GetProcAddress(k32, "K32EnumProcessModulesEx"));
        auto modName = reinterpret_cast<ModName>(GetProcAddress(k32, "K32GetModuleFileNameExW"));
        if (enumMods && modName && enumMods(pi.hProcess, mods, sizeof(mods), &need, 0x02 /* LIST_MODULES_64BIT */))
            for (DWORD i = 0; i < need / sizeof(HMODULE); ++i) { wchar_t p[MAX_PATH]; if (modName(pi.hProcess, mods[i], p, MAX_PATH) && !_wcsicmp(p, shim.c_str())) { remoteBase = reinterpret_cast<uintptr_t>(mods[i]); break; } }
    }
    if (!remoteBase) return fail("shim not found in game modules");
    void* remoteInit = reinterpret_cast<void*>(remoteBase + (init - reinterpret_cast<uintptr_t>(local)));
    FreeLibrary(local);
    if (!RunRemote(pi.hProcess, remoteInit, nullptr, rc, 30000)) return fail("InitializeShim thread failed");
    Log("InitializeShim exit=%08lX", rc);
    if (rc) return fail("InitializeShim reported failure");
    ResumeThread(pi.hThread);
    Log("game running");

    DWORD wait = WaitForSingleObject(pi.hProcess, seconds ? seconds * 1000 : INFINITE);
    if (wait == WAIT_TIMEOUT) { Log("time limit reached; closing game"); TerminateProcess(pi.hProcess, 0); WaitForSingleObject(pi.hProcess, 10000); }
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code); Log("game exit=%08lX", code);
    Sleep(300); g_stop = true; if (capture.joinable()) capture.join();
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return 0;
}
