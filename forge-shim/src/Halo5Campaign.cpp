// Halo5Campaign.exe: the player-facing launcher on Windows. Lives next to halo5forge.exe in the prepared game folder,
// needs no arguments, and only starts the game: the compatibility layer is loaded inside the game process by the
// in-process loader (memory_release_uwp.dll, see Bootstrap.cpp), which also applies h5campaign\settings.ini, the
// portable profile and the logs. On the PS5, prospero-win starts halo5forge.exe directly (it can't create processes).
#include <windows.h>
#include <string>

namespace {
int Fail(const wchar_t* text) { MessageBoxW(nullptr, text, L"Osiris", MB_OK | MB_ICONERROR); return 1; }
bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring root = self; root = root.substr(0, root.find_last_of(L'\\'));
    std::wstring exe = root + L"\\halo5forge.exe";
    if (!Exists(exe)) return Fail(L"halo5forge.exe was not found next to this launcher.\n\nRun Osiris.exe from the prepared game folder.");
    if (!Exists(root + L"\\Memory_Release_UWP_343.dll") || !Exists(root + L"\\h5campaign\\ForgeDesktopShim.dll"))
        return Fail(L"This game folder is missing the compatibility layer (Memory_Release_UWP_343.dll or h5campaign\\ForgeDesktopShim.dll).\n\nRe-run the setup tool.");
    std::wstring cmd = L"\"" + exe + L"\"";
    STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, root.c_str(), &si, &pi)) {
        wchar_t text[160]; wsprintfW(text, L"The game could not be started (error %lu).", GetLastError()); return Fail(text);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}
