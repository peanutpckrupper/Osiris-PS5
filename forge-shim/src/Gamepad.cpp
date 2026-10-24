// Windows.Gaming.Input.Gamepad replacement: one always-present gamepad whose reading comes from
// XInput controller 0 (real pad, works without window focus and under Wine) OR'ed with synthetic
// button presses (g_padSynth, driven by FORGE_AUTOKEYS "P<hex>" entries for automated runs).
#include <windows.h>
#include <wrl.h>
#include <windows.gaming.input.h>
#include <windows.foundation.collections.h>
#include <atomic>
#include <cstdio>
#include <vector>
#include <thread>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::Gaming::Input;

std::atomic<unsigned> g_padSynth{0};   // GamepadButtons bits held by automation

namespace {
struct XState { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
struct XVib { WORD left, right; };
using XGetState = DWORD(WINAPI*)(DWORD, XState*);
using XSetState = DWORD(WINAPI*)(DWORD, XVib*);
XGetState g_get = nullptr; XSetState g_set = nullptr;
void LoadXInput() {
    static bool done = false; if (done) return; done = true;
    for (auto name : { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" })
        if (HMODULE m = LoadLibraryW(name)) { g_get = reinterpret_cast<XGetState>(GetProcAddress(m, "XInputGetState")); g_set = reinterpret_cast<XSetState>(GetProcAddress(m, "XInputSetState")); if (g_get) break; }
    OutputDebugStringA(g_get ? "[ForgeShim] Gamepad: XInput loaded\n" : "[ForgeShim] Gamepad: XInput unavailable (synthetic input only)\n");
    if (g_get) { char line[96]; int n = sprintf_s(line, "[ForgeShim] Gamepad: XInput slots connected:");
        for (int i = 0; i < 4; ++i) { XState x{}; if (g_get(i, &x) == 0) n += sprintf_s(line + n, sizeof(line) - n, " %d", i); }
        strcat_s(line, "\n"); OutputDebugStringA(line); }
}
// The first connected XInput slot (0-3), remembered between reads. A controller isn't always in slot 0: another
// (virtual or disconnected) pad can hold it, and then reading only slot 0 sees nothing.
std::atomic<int> g_slot{0};
bool ReadPad(XState& x)
{
    if (!g_get) return false;
    int s = g_slot.load(); if (g_get(s, &x) == 0) return true;
    static std::atomic<ULONGLONG> lastScan{0}; ULONGLONG now = GetTickCount64();
    if (now - lastScan.load() < 1000) return false;   // XInputGetState on an empty slot is slow; rescan at most once a second
    lastScan = now;
    for (int i = 0; i < 4; ++i) if (i != s && g_get(i, &x) == 0)
    { g_slot = i; char line[64]; sprintf_s(line, "[ForgeShim] Gamepad: using XInput slot %d\n", i); OutputDebugStringA(line); return true; }
    return false;
}
double Axis(SHORT v) { return v < 0 ? v / 32768.0 : v / 32767.0; }

}  // namespace

// Exit combo: Forge has no Quit entry (a Store app is closed by Windows), and in fullscreen there's no close button.
// Holding View + LB + RB (Back + both bumpers; Share + L1 + R1 on a PlayStation pad) for 3 seconds closes the game
// through the window's normal close path. Not View + Menu: holding those is the mode-switch combo on some controllers
// (GameSir pads change from Xbox to PS4 mode and vanish from XInput). Polls XInput directly, so it also works on
// loading screens. FORGE_EXIT_COMBO=0 (or off) turns it off; FORGE_EXIT_COMBO=0x<mask> uses other XInput buttons.
void StartExitCombo(HWND hwnd)
{
    wchar_t v[16]{}; WORD mask = 0x0020 | 0x0100 | 0x0200;   // XINPUT_GAMEPAD_BACK | LEFT_SHOULDER | RIGHT_SHOULDER
    if (GetEnvironmentVariableW(L"FORGE_EXIT_COMBO", v, 16))
    {
        if ((v[0] == L'0' && v[1] == 0) || !_wcsicmp(v, L"off")) return;
        if (v[0] == L'0' && (v[1] == L'x' || v[1] == L'X')) { auto m = static_cast<WORD>(wcstoul(v + 2, nullptr, 16)); if (m) mask = m; }
    }
    LoadXInput(); if (!g_get) return;
    std::thread([hwnd, mask]() {
        ULONGLONG since = 0;
        for (;;) {
            Sleep(50); XState x{};
            bool held = ReadPad(x) && (x.buttons & mask) == mask;
            if (!held) { since = 0; continue; }
            if (!since) since = GetTickCount64();
            if (GetTickCount64() - since >= 3000) { OutputDebugStringA("[ForgeShim] exit combo held: closing the game\n"); PostMessageW(hwnd, WM_CLOSE, 0, 0); return; }
        }
    }).detach();
}
namespace {
class Pad final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IGamepad, IGamepad2, IGameController> {
    InspectableClass(L"Windows.Gaming.Input.Gamepad", BaseTrust)
    GamepadVibration vib_{};
    INT64 next_ = 1;
public:
    HRESULT STDMETHODCALLTYPE get_Vibration(GamepadVibration* v) override { *v = vib_; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Vibration(GamepadVibration v) override {
        vib_ = v; if (g_set) { XVib x{ static_cast<WORD>(v.LeftMotor * 65535), static_cast<WORD>(v.RightMotor * 65535) }; g_set(g_slot.load(), &x); } return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCurrentReading(GamepadReading* r) override {
        if (!r) return E_POINTER; *r = GamepadReading{};
        LARGE_INTEGER q, f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f); r->Timestamp = static_cast<UINT64>(q.QuadPart * 1000000.0 / f.QuadPart);
        unsigned b = 0; XState x{};
        if (ReadPad(x)) {
            static const struct { WORD xi; unsigned wgi; } map[] = {
                {0x0010, 0x1}, {0x0020, 0x2}, {0x1000, 0x4}, {0x2000, 0x8}, {0x4000, 0x10}, {0x8000, 0x20},
                {0x0001, 0x40}, {0x0002, 0x80}, {0x0004, 0x100}, {0x0008, 0x200}, {0x0100, 0x400}, {0x0200, 0x800}, {0x0040, 0x1000}, {0x0080, 0x2000} };
            for (auto& m : map) if (x.buttons & m.xi) b |= m.wgi;
            r->LeftTrigger = x.lt / 255.0; r->RightTrigger = x.rt / 255.0;
            r->LeftThumbstickX = Axis(x.lx); r->LeftThumbstickY = Axis(x.ly); r->RightThumbstickX = Axis(x.rx); r->RightThumbstickY = Axis(x.ry);
        }
        b |= g_padSynth.load();
        r->Buttons = static_cast<GamepadButtons>(b);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetButtonLabel(GamepadButtons, ABI::Windows::Gaming::Input::GameControllerButtonLabel* v) override { *v = GameControllerButtonLabel_None; return S_OK; }
    HRESULT STDMETHODCALLTYPE add_HeadsetConnected(ITypedEventHandler<IGameController*, Headset*>*, EventRegistrationToken* t) override { t->value = next_++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_HeadsetConnected(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_HeadsetDisconnected(ITypedEventHandler<IGameController*, Headset*>*, EventRegistrationToken* t) override { t->value = next_++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_HeadsetDisconnected(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_UserChanged(ITypedEventHandler<IGameController*, ABI::Windows::System::UserChangedEventArgs*>*, EventRegistrationToken* t) override { t->value = next_++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_UserChanged(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Headset(IHeadset** v) override { *v = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsWireless(boolean* v) override { *v = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_User(ABI::Windows::System::IUser** v) override { *v = nullptr; return S_OK; }
};

class PadList final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IVectorView<Gamepad*>, IIterable<Gamepad*>> {
    InspectableClass(L"ForgeShim.GamepadList", BaseTrust)
    ComPtr<IGamepad> pad_;
public:
    explicit PadList(IGamepad* p) : pad_(p) {}
    HRESULT STDMETHODCALLTYPE GetAt(unsigned i, IGamepad** v) override { if (i) return E_BOUNDS; return pad_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* n) override { *n = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE IndexOf(IGamepad* p, unsigned* i, boolean* f) override { *i = 0; *f = p == pad_.Get(); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMany(unsigned start, unsigned cap, IGamepad** items, unsigned* got) override { *got = 0; if (!start && cap) { pad_.CopyTo(&items[0]); *got = 1; } return S_OK; }
    HRESULT STDMETHODCALLTYPE First(IIterator<Gamepad*>** v) override { *v = nullptr; return E_NOTIMPL; }
};

class Statics final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, IGamepadStatics, IGamepadStatics2> {
    ComPtr<Pad> pad_ = Make<Pad>();
    std::vector<std::pair<INT64, ComPtr<IEventHandler<Gamepad*>>>> added_;
    INT64 next_ = 1;
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** v) override { *v = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE add_GamepadAdded(IEventHandler<Gamepad*>* h, EventRegistrationToken* t) override {
        if (!h || !t) return E_INVALIDARG; t->value = next_++; added_.push_back({ t->value, h });
        ComPtr<IEventHandler<Gamepad*>> handler(h); ComPtr<IGamepad> pad; pad_.As(&pad);
        std::thread([handler, pad]() { Sleep(200); handler->Invoke(nullptr, pad.Get()); OutputDebugStringA("[ForgeShim] Gamepad: GamepadAdded raised\n"); }).detach();
        OutputDebugStringA("[ForgeShim] Gamepad: add_GamepadAdded\n"); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE remove_GamepadAdded(EventRegistrationToken t) override { for (auto it = added_.begin(); it != added_.end(); ++it) if (it->first == t.value) { added_.erase(it); break; } return S_OK; }
    HRESULT STDMETHODCALLTYPE add_GamepadRemoved(IEventHandler<Gamepad*>*, EventRegistrationToken* t) override { t->value = next_++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_GamepadRemoved(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Gamepads(IVectorView<Gamepad*>** v) override { ComPtr<IGamepad> pad; pad_.As(&pad); auto l = Make<PadList>(pad.Get()); return l.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE FromGameController(IGameController* c, IGamepad** v) override { ComPtr<IGamepad> pad; pad_.As(&pad); if (c && pad) { ComPtr<IGameController> mine; pad.As(&mine); if (mine.Get() == c) return pad.CopyTo(v); } *v = nullptr; return S_OK; }
};
}

HRESULT GamepadActivation(REFIID iid, void** out) { LoadXInput(); static ComPtr<Statics> s = Make<Statics>(); return s ? s.CopyTo(iid, out) : E_OUTOFMEMORY; }
