// Package identity for halo5onpc's in-game helpers: GetCurrentPackageFullName/FamilyName answer with the
// Forge package identity, but only when called from an h5sololauncher.* module. Everything else in the
// game still sees an unpackaged process.
#include <windows.h>
#include <appmodel.h>
#include <intrin.h>
#include <cstring>
#include <cwchar>
#include "../third_party/minhook/include/MinHook.h"

namespace {
const wchar_t kFullName[] = L"Microsoft.Halo5Forge_1.194.6192.2_x64__8wekyb3d8bbwe";
const wchar_t kFamily[] = L"Microsoft.Halo5Forge_8wekyb3d8bbwe";
using GetName = LONG(WINAPI*)(UINT32*, PWSTR);
GetName g_fullOriginal = nullptr, g_familyOriginal = nullptr;

bool CallerIsHelper(void* returnAddress) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(returnAddress), &m) || !m) return false;
    wchar_t path[MAX_PATH]; if (!GetModuleFileNameW(m, path, MAX_PATH)) return false;
    const wchar_t* name = wcsrchr(path, L'\\'); name = name ? name + 1 : path;
    return _wcsnicmp(name, L"h5sololauncher.", 15) == 0;
}
LONG Answer(const wchar_t* value, UINT32* length, PWSTR buffer) {
    if (!length) return ERROR_INVALID_PARAMETER;
    UINT32 need = static_cast<UINT32>(wcslen(value) + 1);
    if (!buffer || *length < need) { *length = need; return ERROR_INSUFFICIENT_BUFFER; }
    wcscpy_s(buffer, *length, value); *length = need; return ERROR_SUCCESS;
}
LONG WINAPI FullName(UINT32* length, PWSTR buffer) {
    if (CallerIsHelper(_ReturnAddress())) return Answer(kFullName, length, buffer);
    return g_fullOriginal(length, buffer);
}
LONG WINAPI Family(UINT32* length, PWSTR buffer) {
    if (CallerIsHelper(_ReturnAddress())) return Answer(kFamily, length, buffer);
    return g_familyOriginal(length, buffer);
}
}

void InstallPackageIdentity() {
    if (GetEnvironmentVariableW(L"FORGE_NO_HELPER_IDENTITY", nullptr, 0)) return;
    MH_STATUS st = MH_Initialize(); if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return;
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll"); if (!kb) return;
    void* full = reinterpret_cast<void*>(GetProcAddress(kb, "GetCurrentPackageFullName"));
    void* family = reinterpret_cast<void*>(GetProcAddress(kb, "GetCurrentPackageFamilyName"));
    int ok = 0;
    if (full && MH_CreateHook(full, reinterpret_cast<void*>(&FullName), reinterpret_cast<void**>(&g_fullOriginal)) == MH_OK && MH_EnableHook(full) == MH_OK) ++ok;
    if (family && MH_CreateHook(family, reinterpret_cast<void*>(&Family), reinterpret_cast<void**>(&g_familyOriginal)) == MH_OK && MH_EnableHook(family) == MH_OK) ++ok;
    OutputDebugStringA(ok == 2 ? "[ForgeShim] package identity for h5sololauncher helpers installed\n" : "[ForgeShim] package identity hooks incomplete\n");
}
