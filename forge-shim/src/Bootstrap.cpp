// Bootstrap: the compatibility layer's in-process loader, installed in the game folder as memory_release_uwp.dll (the
// first DLL halo5forge.exe imports). Forge's own memory_release_uwp.dll is kept beside it as Memory_Release_UWP_343.dll
// and every export is forwarded to it, so the game sees the same library.
//
// Why: prospero-win (Wine on the PS5) cannot create processes, so a launcher that starts halo5forge.exe suspended and
// injects the layer from outside can't work there. Loaded as an import, this runs inside the game process instead:
// its DllMain only redirects the exe's entry point; when the main thread reaches it (imports and TLS callbacks done, as
// with the launcher's spin at the entry point) the stub prepares the environment, loads
// h5campaign\ForgeDesktopShim.dll, runs InitializeShim, puts the entry bytes back and continues into the game.
//   settings      production FORGE_* defaults, then h5campaign\settings.ini (KEY=VALUE; 0 or empty = off; FORGE_*, VKD3D_*, DXVK_*)
//   profile       LOCALAPPDATA/TEMP = h5campaign\profile\local and \temp (saves travel with the folder)
//   logs          h5campaign\logs\game.log (game-prev.log keeps the run before), launcher.log (this loader)
// The same path is used on Windows (Halo5Campaign.exe just starts halo5forge.exe) and on the PS5 (prospero-win starts
// halo5forge.exe directly).
#include <windows.h>
#include <string>
#include <cstdio>

#pragma comment(linker, "/export:?AlignedAllocUntracked@Memory@i343@@YAPEAX_KW4AlignmentBit@12@@Z=Memory_Release_UWP_343.?AlignedAllocUntracked@Memory@i343@@YAPEAX_KW4AlignmentBit@12@@Z")
#pragma comment(linker, "/export:?AlignedFreeUntracked@Memory@i343@@YAXPEAX@Z=Memory_Release_UWP_343.?AlignedFreeUntracked@Memory@i343@@YAXPEAX@Z")
#pragma comment(linker, "/export:?AlignedReallocUntracked@Memory@i343@@YAPEAXPEAX_KW4AlignmentBit@12@@Z=Memory_Release_UWP_343.?AlignedReallocUntracked@Memory@i343@@YAPEAXPEAX_KW4AlignmentBit@12@@Z")
#pragma comment(linker, "/export:?AlignedSizeUntracked@Memory@i343@@YA_KPEAXW4AlignmentBit@12@@Z=Memory_Release_UWP_343.?AlignedSizeUntracked@Memory@i343@@YA_KPEAXW4AlignmentBit@12@@Z")
#pragma comment(linker, "/export:?AllocUntracked@Memory@i343@@YAPEAX_K@Z=Memory_Release_UWP_343.?AllocUntracked@Memory@i343@@YAPEAX_K@Z")
#pragma comment(linker, "/export:?FreeUntracked@Memory@i343@@YAXPEAX@Z=Memory_Release_UWP_343.?FreeUntracked@Memory@i343@@YAXPEAX@Z")
#pragma comment(linker, "/export:?ReallocUntracked@Memory@i343@@YAPEAXPEAX_K@Z=Memory_Release_UWP_343.?ReallocUntracked@Memory@i343@@YAPEAXPEAX_K@Z")
#pragma comment(linker, "/export:?RegisterOutOfMemoryCallback@Control@Memory@i343@@YAX$$QEAV?$function@$$A6AX_K000W4Pool@Memory@i343@@W4Category@23@@Z@std@@@Z=Memory_Release_UWP_343.?RegisterOutOfMemoryCallback@Control@Memory@i343@@YAX$$QEAV?$function@$$A6AX_K000W4Pool@Memory@i343@@W4Category@23@@Z@std@@@Z")
#pragma comment(linker, "/export:?ReportOutOfHeapMemory@Control@Memory@i343@@YAX_KW4Pool@23@W4Category@23@@Z=Memory_Release_UWP_343.?ReportOutOfHeapMemory@Control@Memory@i343@@YAX_KW4Pool@23@W4Category@23@@Z")
#pragma comment(linker, "/export:?SizeUntracked@Memory@i343@@YA_KPEAX@Z=Memory_Release_UWP_343.?SizeUntracked@Memory@i343@@YA_KPEAX@Z")
#pragma comment(linker, "/export:?VirtualAllocUntracked@Memory@i343@@YAPEAXPEAX_KKK@Z=Memory_Release_UWP_343.?VirtualAllocUntracked@Memory@i343@@YAPEAXPEAX_KKK@Z")
#pragma comment(linker, "/export:?VirtualFreeUntracked@Memory@i343@@YA_NPEAX_KK@Z=Memory_Release_UWP_343.?VirtualFreeUntracked@Memory@i343@@YA_NPEAX_KK@Z")
#pragma comment(linker, "/export:i343LibraryStub=Memory_Release_UWP_343.i343LibraryStub,DATA")

namespace {
std::wstring g_root, g_log;
BYTE* g_entry = nullptr; BYTE g_saved[12]{};

void Note(const wchar_t* fmt, ...) {
    wchar_t line[1024]; va_list a; va_start(a, fmt); _vsnwprintf_s(line, _TRUNCATE, fmt, a); va_end(a);
    if (g_log.empty()) return;   // plain UTF-8 via WriteFile: no CRT text-mode differences (Windows / Wine)
    char utf8[3072]; int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8) - 2, nullptr, nullptr); if (n <= 0) return;
    utf8[n - 1] = '\r'; utf8[n] = '\n';
    HANDLE h = CreateFileW(g_log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) { DWORD put = 0; WriteFile(h, utf8, static_cast<DWORD>(n + 1), &put, nullptr); CloseHandle(h); }
}
[[noreturn]] void Fail(const wchar_t* fmt, ...) {
    wchar_t line[1024]; va_list a; va_start(a, fmt); _vsnwprintf_s(line, _TRUNCATE, fmt, a); va_end(a);
    Note(L"error: %s", line);
    MessageBoxW(nullptr, line, L"Osiris", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}
std::wstring Trim(std::wstring s) {
    size_t a = s.find_first_not_of(L" \t\r\n"), b = s.find_last_not_of(L" \t\r\n");
    return a == std::wstring::npos ? L"" : s.substr(a, b - a + 1);
}
// Production defaults (the switches every verified campaign run used), then settings.ini on top.
void ApplySettings() {
    const wchar_t* defaults[][2] = {
        { L"FORGE_ENGAGE", L"1" }, { L"FORGE_WAM_SHAPE", L"full" }, { L"FORGE_AUTOSTART", L"1" },
        { L"FORGE_SERVICELINK_OFFLINE", L"1" }, { L"FORGE_OFFLINE_ERRORS", L"1" }, { L"FORGE_OFFLINE_LOBBY", L"1" },
        { L"FORGE_UI_RETAIN", L"1" }, { L"FORGE_TRACE_EXC", L"1" }, { L"FORGE_ENGINE_LOG", L"1" }, { L"FORGE_ENGINE_SKIP", L"6.7" },
    };
    for (auto& d : defaults) SetEnvironmentVariableW(d[0], d[1]);
    std::wstring ini = g_root + L"\\h5campaign\\settings.ini";
    HANDLE h = CreateFileW(ini.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    std::string bytes(static_cast<size_t>(GetFileSize(h, nullptr) & 0xFFFFF), '\0'); DWORD got = 0;
    ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr); CloseHandle(h); bytes.resize(got);
    if (bytes.rfind("\xEF\xBB\xBF", 0) == 0) bytes.erase(0, 3);
    int wn = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring text(wn, L'\0'); MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), wn);
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find(L'\n', at); if (end == std::wstring::npos) end = text.size();
        std::wstring line = Trim(text.substr(at, end - at)); at = end + 1;
        if (line.empty() || line[0] == L'#' || line[0] == L';' || line[0] == L'[') continue;
        size_t eq = line.find(L'='); if (eq == std::wstring::npos) continue;
        std::wstring key = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
        // FORGE_* are ours; VKD3D_* and DXVK_* reach the graphics layers (Wine's PE side reads the process environment).
        if (key.rfind(L"FORGE_", 0) != 0 && key.rfind(L"VKD3D_", 0) != 0 && key.rfind(L"DXVK_", 0) != 0) continue;
        SetEnvironmentVariableW(key.c_str(), value.empty() || value == L"0" ? nullptr : value.c_str());   // 0 or empty = off
        Note(L"setting %s=%s", key.c_str(), value.c_str());
    }
}

void Boot() {
    std::wstring dir = g_root + L"\\h5campaign", logs = dir + L"\\logs", profile = dir + L"\\profile";
    for (auto& d : { dir, logs, profile, profile + L"\\local", profile + L"\\temp" }) CreateDirectoryW(d.c_str(), nullptr);
    g_log = logs + L"\\launcher.log"; DeleteFileW(g_log.c_str());
    Note(L"in-process loader, pid %lu", GetCurrentProcessId());
    std::wstring shim = dir + L"\\ForgeDesktopShim.dll";
    if (GetFileAttributesW(shim.c_str()) == INVALID_FILE_ATTRIBUTES) Fail(L"h5campaign\\ForgeDesktopShim.dll is missing. Re-run the setup tool.");
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Halo5CampaignLauncher");   // kept for the life of the process
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) Fail(L"Osiris is already running.");
    ApplySettings();
    std::wstring game = logs + L"\\game.log", prev = logs + L"\\game-prev.log";
    MoveFileExW(game.c_str(), prev.c_str(), MOVEFILE_REPLACE_EXISTING);
    SetEnvironmentVariableW(L"FORGE_LOG_FILE", game.c_str());
    SetEnvironmentVariableW(L"LOCALAPPDATA", (profile + L"\\local").c_str());
    SetEnvironmentVariableW(L"TEMP", (profile + L"\\temp").c_str()); SetEnvironmentVariableW(L"TMP", (profile + L"\\temp").c_str());
    HMODULE m = LoadLibraryW(shim.c_str());
    if (!m) Fail(L"The compatibility layer did not load (error %lu).\n\nSee h5campaign\\logs.", GetLastError());
    auto init = reinterpret_cast<DWORD(WINAPI*)(void*)>(GetProcAddress(m, "InitializeShim"));
    if (!init) Fail(L"The compatibility layer is damaged (no InitializeShim). Re-run the setup tool.");
    DWORD rc = init(nullptr);
    Note(L"InitializeShim %08lX", rc);
    if (rc) Fail(L"The compatibility layer refused this game version (see h5campaign\\logs\\game.log).");
    Note(L"game running");
}

// Reached instead of the exe's entry point, on the main thread, with the entry's own arguments.
extern "C" DWORD WINAPI EntryStub(void* arg) {
    DWORD old = 0;
    VirtualProtect(g_entry, sizeof(g_saved), PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_entry, g_saved, sizeof(g_saved));
    VirtualProtect(g_entry, sizeof(g_saved), old, &old); FlushInstructionCache(GetCurrentProcess(), g_entry, sizeof(g_saved));
    if (!GetModuleHandleW(L"ForgeDesktopShim.dll")) Boot();   // an older launcher may already have injected the layer
    return reinterpret_cast<DWORD(WINAPI*)(void*)>(g_entry)(arg);
}

bool Redirect() {
    auto base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    if (!nt->OptionalHeader.AddressOfEntryPoint) return false;
    g_entry = base + nt->OptionalHeader.AddressOfEntryPoint;
    BYTE jump[12] = { 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0 };   // mov rax, EntryStub ; jmp rax
    auto target = reinterpret_cast<UINT64>(&EntryStub); memcpy(jump + 2, &target, 8);
    DWORD old = 0;
    if (!VirtualProtect(g_entry, sizeof(jump), PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(g_saved, g_entry, sizeof(g_saved)); memcpy(g_entry, jump, sizeof(jump));
    VirtualProtect(g_entry, sizeof(jump), old, &old); FlushInstructionCache(GetCurrentProcess(), g_entry, sizeof(jump));
    return true;
}
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_root = exe; g_root = g_root.substr(0, g_root.find_last_of(L'\\'));
    const wchar_t* name = wcsrchr(exe, L'\\'); name = name ? name + 1 : exe;
    if (_wcsicmp(name, L"halo5forge.exe")) return TRUE;   // only the game itself gets the layer
    wchar_t off[4]{};
    if (GetEnvironmentVariableW(L"H5CAMPAIGN_NO_LAYER", off, 4)) return TRUE;   // plain Forge (diagnostics)
    Redirect();
    return TRUE;
}
