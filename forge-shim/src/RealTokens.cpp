// Real Xbox Live tokens for the fake account broker (FORGE_REAL_TOKENS=1).
// tools\xbl-probe.ps1 signs the user in (device code), then writes into <profile>\xbl:
//   shim-tokens.txt  line 1 "#identity <TAB> xuid <TAB> gamertag <TAB> agegroup <TAB> privileges",
//                    then "relying party <TAB> expiry <TAB> XBL3.0 x=<uhs>;<xsts token>" per relying party
//   shim-nsal.txt    "host <TAB> fqdn|wildcard <TAB> path <TAB> relying party" (Xbox Live endpoint rules)
// The broker maps each request Url to its relying party and returns that Authorization value.
// Files are re-read when shim-tokens.txt changes, so a refresh during play is picked up.
// Directory: FORGE_XBL_DIR, else %LOCALAPPDATA%\..\xbl (LOCALAPPDATA is <profile>\local for standalone Forge).
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <cwctype>
#include <cstdio>

namespace {
struct Rule { std::wstring host; bool wildcard; std::wstring path; std::wstring rp; };
SRWLOCK g_lock = SRWLOCK_INIT;
FILETIME g_stamp{};
std::vector<Rule> g_rules;
std::map<std::wstring, std::wstring> g_tokens;   // relying party -> Authorization value
std::wstring g_xuid, g_gamertag, g_age, g_privileges;

void Trace(const char* fmt, ...) { char b[700]; va_list a; va_start(a, fmt); _vsnprintf_s(b, _TRUNCATE, fmt, a); va_end(a); OutputDebugStringA("[ForgeShim] XBL "); OutputDebugStringA(b); OutputDebugStringA("\n"); }
std::wstring Lower(std::wstring s) { for (auto& c : s) c = static_cast<wchar_t>(towlower(c)); return s; }
std::wstring Dir() {
    wchar_t b[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"FORGE_XBL_DIR", b, MAX_PATH); if (n && n < MAX_PATH) return b;
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", b, MAX_PATH); if (!n || n >= MAX_PATH) return L"";
    return std::wstring(b) + L"\\..\\xbl";
}
std::vector<std::wstring> Lines(const std::wstring& path) {
    std::vector<std::wstring> out;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return out;
    LARGE_INTEGER size{}; GetFileSizeEx(f, &size);
    if (size.QuadPart <= 0 || size.QuadPart > 8 * 1024 * 1024) { CloseHandle(f); return out; }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0'); DWORD got = 0;
    BOOL ok = ReadFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr); CloseHandle(f);
    if (!ok) return out; bytes.resize(got);
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) bytes.erase(0, 3);
    int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring text(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), n);
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start); if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start); while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        if (!line.empty()) out.push_back(line);
        start = end + 1;
    }
    return out;
}
std::vector<std::wstring> Split(const std::wstring& s) {
    std::vector<std::wstring> parts; size_t start = 0;
    for (;;) { size_t t = s.find(L'\t', start); parts.push_back(s.substr(start, t == std::wstring::npos ? std::wstring::npos : t - start)); if (t == std::wstring::npos) break; start = t + 1; }
    return parts;
}
// Caller holds the lock exclusively.
void ReloadIfChanged() {
    std::wstring dir = Dir(); if (dir.empty()) return;
    std::wstring tokensPath = dir + L"\\shim-tokens.txt";
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(tokensPath.c_str(), GetFileExInfoStandard, &info)) { static bool warned = false; if (!warned) { warned = true; Trace("no token file at %ls - run tools\\xbl-probe.ps1", tokensPath.c_str()); } return; }
    if (CompareFileTime(&info.ftLastWriteTime, &g_stamp) == 0) return;
    g_stamp = info.ftLastWriteTime;
    g_tokens.clear(); g_rules.clear();
    for (auto& line : Lines(tokensPath)) {
        auto p = Split(line);
        if (p[0] == L"#identity" && p.size() >= 5) { g_xuid = p[1]; g_gamertag = p[2]; g_age = p[3]; g_privileges = p[4]; }
        else if (p.size() >= 3 && p[0][0] != L'#') g_tokens[p[0]] = p[2];
    }
    for (auto& line : Lines(dir + L"\\shim-nsal.txt")) {
        auto p = Split(line); if (p.size() < 4) continue;
        g_rules.push_back({ Lower(p[0]), p[1] == L"wildcard", Lower(p[2]), p[3] });
    }
    if (!g_gamertag.empty()) SetEnvironmentVariableW(L"FORGE_GAMERTAG", g_gamertag.c_str());
    if (!g_xuid.empty()) SetEnvironmentVariableW(L"FORGE_XUID", g_xuid.c_str());
    Trace("loaded %zu relying-party tokens, %zu host rules for %ls (%ls)", g_tokens.size(), g_rules.size(), g_gamertag.c_str(), g_xuid.c_str());
}
// Best NSAL match: exact host beats wildcard, longer host and longer path beat shorter.
std::wstring RelyingParty(const std::wstring& url) {
    std::wstring u = Lower(url); size_t s = u.find(L"://"); s = s == std::wstring::npos ? 0 : s + 3;
    size_t e = u.find_first_of(L"/:?", s); std::wstring host = u.substr(s, e == std::wstring::npos ? std::wstring::npos : e - s);
    size_t slash = u.find(L'/', s); std::wstring path = slash == std::wstring::npos ? L"/" : u.substr(slash);
    const Rule* best = nullptr; size_t bestScore = 0;
    for (auto& r : g_rules) {
        bool hostOk = r.wildcard ? (r.host.size() > 1 && r.host[0] == L'*' && host.size() > r.host.size() - 1 && host.compare(host.size() - (r.host.size() - 1), r.host.size() - 1, r.host, 1) == 0)
                                 : host == r.host;
        if (!hostOk || (!r.path.empty() && path.compare(0, r.path.size(), r.path) != 0)) continue;
        size_t score = (r.wildcard ? 0 : 100000) + r.host.size() * 100 + r.path.size();
        if (!best || score > bestScore) { best = &r; bestScore = score; }
    }
    return best ? best->rp : L"http://xboxlive.com";
}
}

bool RealTokensEnabled() { static bool on = GetEnvironmentVariableW(L"FORGE_REAL_TOKENS", nullptr, 0) != 0; return on; }

// Fills the Authorization value and identity for a request Url. Returns false if no token is available.
bool RealTokenFor(const std::wstring& url, std::wstring& authorization, std::wstring& xuid, std::wstring& gamertag, std::wstring& age, std::wstring& privileges) {
    AcquireSRWLockExclusive(&g_lock);
    ReloadIfChanged();
    std::wstring rp = RelyingParty(url);
    auto it = g_tokens.find(rp);
    bool found = it != g_tokens.end();
    if (!found) { auto core = g_tokens.find(L"http://xboxlive.com"); if (core != g_tokens.end()) { it = core; found = true; Trace("no token for %ls (%ls); using http://xboxlive.com", rp.c_str(), url.c_str()); } }
    if (found) authorization = it->second;
    xuid = g_xuid; gamertag = g_gamertag; age = g_age; privileges = g_privileges;
    ReleaseSRWLockExclusive(&g_lock);
    static std::map<std::wstring, unsigned> seen; unsigned count = 0;   // log the first request per Url, then every 1000th
    AcquireSRWLockExclusive(&g_lock); count = ++seen[url]; ReleaseSRWLockExclusive(&g_lock);
    if (found && (count == 1 || count % 1000 == 0)) Trace("token for %ls -> %ls (request %u)", url.c_str(), rp.c_str(), count);
    return found && !xuid.empty();
}
