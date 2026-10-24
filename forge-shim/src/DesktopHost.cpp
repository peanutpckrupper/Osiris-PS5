// Derived from the local WinDurango KernelX NativeDesktopHost.cpp.
// WinDurango contributors; LGPL-3.0, see LICENSE-WinDurango.
// Modified for Forge's desktop ABI experiment; activation follows Load.
#include "NativeDesktopHost.h"
#include "NativeDispatchQueue.h"
#include "Pointer.h"

#include <windows.applicationmodel.activation.h>
#include <windows.foundation.h>
#include <CoreWindow.h>
#include <winstring.h>
#include <wrl.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <new>
#include <utility>
#include <vector>
#include <thread>
#include <string>

using namespace Microsoft::WRL;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::ApplicationModel;
using namespace ABI::Windows::ApplicationModel::Core;

void CampaignSelectMission(int mission);   // CampaignEngine.cpp (automation)
namespace h5::desktop
{
namespace
{
void Trace(char const* msg) noexcept
{
    OutputDebugStringA(msg);
}

void TraceHr(char const* phase, HRESULT hr) noexcept
{
    char buf[256];
    sprintf_s(buf, "[H5S09] %s HRESULT=0x%08lX\n", phase,
              static_cast<unsigned long>(hr));
    Trace(buf);
}

HWND g_hwnd = nullptr;
}  // namespace
}  // namespace h5::desktop
extern std::atomic<unsigned> g_padSynth;
void StartExitCombo(HWND hwnd);
namespace h5::desktop {
namespace {
std::atomic<unsigned char> g_synthKeys[256]{};
DWORD g_uiThread = 0;
ComPtr<ICoreWindow> g_window;
ComPtr<ICoreApplicationView> g_view;
std::atomic_bool g_quit{false};
std::shared_ptr<NativeDispatchState> g_dispatchQueue;

// Display mode. FORGE_WINDOW=fullscreen (default: a borderless window covering the monitor; the game's swap chain
// keeps its own resolution and DXGI scales it) or windowed (1280x720, resizable). Alt+Enter toggles; Forge's own
// ApplicationView fullscreen calls go through the same path (posted to the window thread).
constexpr UINT FullscreenMessage = WM_APP + 0x46;
bool g_fullscreen = false; RECT g_windowedRect{};
void ApplyFullscreen(HWND hwnd, bool on)
{
    if (on == g_fullscreen && (GetWindowLongW(hwnd, GWL_STYLE) & WS_POPUP) == (on ? WS_POPUP : 0)) return;
    bool visible = IsWindowVisible(hwnd) != FALSE;
    if (on)
    {
        if (!g_fullscreen) GetWindowRect(hwnd, &g_windowedRect);
        MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | (visible ? WS_VISIBLE : 0));
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : 0));
    }
    else
    {
        SetWindowLongW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | (visible ? WS_VISIBLE : 0));
        RECT r = g_windowedRect;
        if (r.right <= r.left) { r = { 0, 0, 1280, 720 }; AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0); OffsetRect(&r, 80 - r.left, 60 - r.top); }
        SetWindowPos(hwnd, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : 0));
    }
    g_fullscreen = on;
    char line[80]; sprintf_s(line, "[ForgeShim] window: %s\n", on ? "fullscreen (borderless)" : "windowed"); OutputDebugStringA(line);
}
bool ForwardInput(UINT msg, WPARAM wp, LPARAM lp);
bool ForwardPointer(UINT msg, WPARAM wp, LPARAM lp);
void FireActiveAndVisible();
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_APP + 0x45: case WM_ACTIVATE:
        FireActiveAndVisible();
        if (msg == WM_ACTIVATE) return DefWindowProcW(hwnd, msg, wp, lp);
        return 0;
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        ForwardPointer(msg, wp, lp);
        return DefWindowProcW(hwnd, msg, wp, lp);
    case FullscreenMessage:
        ApplyFullscreen(hwnd, wp != 0);
        return 0;
    case WM_SYSCHAR:
        if (wp == VK_RETURN) return 0;   // no beep for Alt+Enter
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_CHAR:
        if (msg == WM_SYSKEYDOWN && wp == VK_RETURN && (lp & (1 << 29)) && !(lp & (1 << 30))) { ApplyFullscreen(hwnd, !g_fullscreen); return 0; }
        if (ForwardInput(msg, wp, lp)) return 0;
        return DefWindowProcW(hwnd, msg, wp, lp);
    case NativeDispatchMessage:
        if(g_dispatchQueue)g_dispatchQueue->ProcessPending();
        return 0;
    case WM_CLOSE:
        // A Store game is closed by Windows; unpackaged, nothing would end the process (it kept running
        // hidden). Hide the window at once, give in-flight saves a moment, then end the process. Saves are
        // written at checkpoints and preferences while playing, so nothing waits on a suspend event.
        OutputDebugStringA("[ForgeShim] window closed: ending Forge in 1.5 s\n");
        ShowWindow(hwnd, SW_HIDE);
        CreateThread(nullptr, 0, [](void*) -> DWORD { Sleep(1500); TerminateProcess(GetCurrentProcess(), 0); return 0; }, nullptr, 0, nullptr);
        return 0;
    case WM_DESTROY:
        if(g_dispatchQueue)g_dispatchQueue->Shutdown();
        g_quit = true;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// Implements the desktop UI message pump. Async dispatch is deliberately
// unimplemented rather than reporting success without executing callbacks.
class NativeDispatcher final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, ICoreDispatcher, ICoreAcceleratorKeys>
{
    std::vector<ComPtr<ITypedEventHandler<CoreDispatcher*, AcceleratorKeyEventArgs*>>> acceleratorHandlers;
public:
    void FireAccelerator(IAcceleratorKeyEventArgs* args) {
        auto copy = acceleratorHandlers;
        for (auto& h : copy) if (h) reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IUnknown*, ICoreDispatcher*, IAcceleratorKeyEventArgs*)>((*reinterpret_cast<void***>(h.Get()))[3])(h.Get(), static_cast<ICoreDispatcher*>(this), args);
    }
    size_t AcceleratorCount() const { return acceleratorHandlers.size(); }
private:
public:
    HRESULT STDMETHODCALLTYPE add_AcceleratorKeyActivated(ITypedEventHandler<CoreDispatcher*, AcceleratorKeyEventArgs*>* handler,EventRegistrationToken* token) override {OutputDebugStringA("[ForgeShim] CoreDispatcher.add_AcceleratorKeyActivated\n");if(!handler||!token)return E_POINTER;acceleratorHandlers.emplace_back(handler);token->value=acceleratorHandlers.size();return S_OK;}
    HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyActivated(EventRegistrationToken token) override {if(token.value>0 && static_cast<size_t>(token.value)<=acceleratorHandlers.size())acceleratorHandlers[token.value-1].Reset();return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {using Base=RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,ICoreDispatcher,ICoreAcceleratorKeys>;HRESULT hr=Base::QueryInterface(iid,out);wchar_t id[64],line[160];StringFromGUID2(iid,id,64);swprintf_s(line,L"[ForgeShim] Dispatcher QI %s hr=%08X\n",id,hr);if(FAILED(hr))OutputDebugStringW(line);return hr;}
    HRESULT STDMETHODCALLTYPE get_HasThreadAccess(boolean* value) override
    {
        if (!value) return E_POINTER;
        *value = static_cast<boolean>(GetCurrentThreadId() == g_uiThread);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ProcessEvents(CoreProcessEventsOption option) override
    {
        if (GetCurrentThreadId() != g_uiThread) return RPC_E_WRONG_THREAD;
        MSG msg{};
        // Preserve documented CoreProcessEventsOption numeric order (0..3).
        if (option == static_cast<CoreProcessEventsOption>(2))
        {
            while (!g_quit && GetMessageW(&msg, nullptr, 0, 0) > 0)
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            return S_OK;
        }
        if (option == static_cast<CoreProcessEventsOption>(1))
        {
            if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT) g_quit = true;
                else { TranslateMessage(&msg); DispatchMessageW(&msg); }
            }
            return S_OK;
        }
        if (option == static_cast<CoreProcessEventsOption>(0))
        {
            if (GetMessageW(&msg, nullptr, 0, 0) <= 0) { g_quit = true; return S_OK; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) { g_quit = true; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RunAsync(CoreDispatcherPriority priority,
        IDispatchedHandler* handler, IAsyncAction** action) override
    {
        Trace("[ForgeShim] CoreDispatcher.RunAsync queued\n");
        return ScheduleNativeCallback(g_dispatchQueue,priority,handler,action);
    }
    HRESULT STDMETHODCALLTYPE RunIdleAsync(IIdleDispatchedHandler*, IAsyncAction** action) override
    {
        if (action) *action = nullptr;
        Trace("[H5S09] Unsupported: CoreDispatcher.RunIdleAsync\n");
        return E_NOTIMPL;
    }
};

// All ABI slots are implemented in Windows SDK order. Event registration is
// retained for later Win32-to-WinRT input/activation forwarding work.
class NativeCoreWindow final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, ICoreWindow, ICoreWindowInterop>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        using Base=RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,ICoreWindow,ICoreWindowInterop>;
        HRESULT hr=Base::QueryInterface(iid,out);wchar_t id[64],line[160];StringFromGUID2(iid,id,64);swprintf_s(line,L"[ForgeShim] CoreWindow QI %s hr=%08X\n",id,hr);if(FAILED(hr))OutputDebugStringW(line);return hr;
    }
    explicit NativeCoreWindow(HWND hwnd) : hwnd_(hwnd), dispatcher_(Make<NativeDispatcher>()) {}

    // Desktop interop required by the WinDurango D3D11X swap-chain bridge.
    HRESULT STDMETHODCALLTYPE get_WindowHandle(HWND* out) override
    {
        if (!out) return E_POINTER;
        *out = hwnd_;
        return IsWindow(hwnd_) ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    }
    HRESULT STDMETHODCALLTYPE put_MessageHandled(boolean) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AutomationHostProvider(IInspectable** v) override { return Null(v); }
    HRESULT STDMETHODCALLTYPE get_Bounds(Rect* v) override
    {
        if (!v) return E_POINTER;
        RECT r{};
        if (!GetClientRect(hwnd_, &r)) return HRESULT_FROM_WIN32(GetLastError());
        *v = Rect{0.f, 0.f, float(r.right-r.left), float(r.bottom-r.top)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CustomProperties(ABI::Windows::Foundation::Collections::IPropertySet** v) override { return Null(v); }
    HRESULT STDMETHODCALLTYPE get_Dispatcher(ICoreDispatcher** v) override { return dispatcher_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_FlowDirection(CoreWindowFlowDirection* v) override
    { if (!v) return E_POINTER; *v = static_cast<CoreWindowFlowDirection>(0); return S_OK; }
    HRESULT STDMETHODCALLTYPE put_FlowDirection(CoreWindowFlowDirection) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsInputEnabled(boolean* v) override
    { if (!v) return E_POINTER; *v = inputEnabled_; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsInputEnabled(boolean v) override { inputEnabled_ = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_PointerCursor(ICoreCursor** v) override { return Null(v); }
    HRESULT STDMETHODCALLTYPE put_PointerCursor(ICoreCursor*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_PointerPosition(Point* v) override
    {
        if (!v) return E_POINTER;
        POINT pt{}; if (!GetCursorPos(&pt) || !ScreenToClient(hwnd_, &pt)) return E_FAIL;
        *v = Point{float(pt.x), float(pt.y)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Visible(boolean* v) override
    { if (!v) return E_POINTER; *v = static_cast<boolean>(IsWindowVisible(hwnd_) != FALSE); return S_OK; }
    HRESULT STDMETHODCALLTYPE Activate() override
    { ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_); Trace("[H5S09] CoreWindow.Activate\n"); return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override { return DestroyWindow(hwnd_) ? S_OK : HRESULT_FROM_WIN32(GetLastError()); }
    HRESULT STDMETHODCALLTYPE GetAsyncKeyState(ABI::Windows::System::VirtualKey key, CoreVirtualKeyStates* v) override
    { int k = static_cast<int>(key) & 0xFF; return Key(static_cast<SHORT>(GetAsyncKeyStateWin32(k) | (g_synthKeys[k] ? 0x8000 : 0)), v); }
    HRESULT STDMETHODCALLTYPE GetKeyState(ABI::Windows::System::VirtualKey key, CoreVirtualKeyStates* v) override
    { int k = static_cast<int>(key) & 0xFF; return Key(static_cast<SHORT>(::GetKeyState(k) | (g_synthKeys[k] ? 0x8000 : 0)), v); }
    HRESULT STDMETHODCALLTYPE ReleasePointerCapture() override { ::ReleaseCapture(); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetPointerCapture() override { ::SetCapture(hwnd_); return S_OK; }

#define H5_EVENT(name, ArgType) \
    HRESULT STDMETHODCALLTYPE add_##name(ITypedEventHandler<CoreWindow*, ArgType*>* h, EventRegistrationToken* t) override \
    { return Register(L"" #name, reinterpret_cast<IUnknown*>(h), t); } \
    HRESULT STDMETHODCALLTYPE remove_##name(EventRegistrationToken t) override { return Unregister(t); }
    H5_EVENT(Activated, WindowActivatedEventArgs)
    H5_EVENT(AutomationProviderRequested, AutomationProviderRequestedEventArgs)
    H5_EVENT(CharacterReceived, CharacterReceivedEventArgs)
    H5_EVENT(Closed, CoreWindowEventArgs)
    H5_EVENT(InputEnabled, InputEnabledEventArgs)
    H5_EVENT(KeyDown, KeyEventArgs)
    H5_EVENT(KeyUp, KeyEventArgs)
    H5_EVENT(PointerCaptureLost, PointerEventArgs)
    H5_EVENT(PointerEntered, PointerEventArgs)
    H5_EVENT(PointerExited, PointerEventArgs)
    H5_EVENT(PointerMoved, PointerEventArgs)
    H5_EVENT(PointerPressed, PointerEventArgs)
    H5_EVENT(PointerReleased, PointerEventArgs)
    H5_EVENT(TouchHitTesting, TouchHitTestingEventArgs)
    H5_EVENT(PointerWheelChanged, PointerEventArgs)
    H5_EVENT(SizeChanged, WindowSizeChangedEventArgs)
    H5_EVENT(VisibilityChanged, VisibilityChangedEventArgs)
#undef H5_EVENT

private:
    template<typename T> static HRESULT Null(T** value)
    { if (!value) return E_POINTER; *value = nullptr; return S_OK; }
    static SHORT GetAsyncKeyStateWin32(int key) { return ::GetAsyncKeyState(key); }
    static HRESULT Key(SHORT state, CoreVirtualKeyStates* v)
    {
        if (!v) return E_POINTER;
        *v = static_cast<CoreVirtualKeyStates>(((state & 0x8000) ? 1 : 0) | ((state & 1) ? 2 : 0));
        return S_OK;
    }
    HRESULT Register(const wchar_t* kind, IUnknown* handler, EventRegistrationToken* token)
    {
        if (!handler || !token) return E_INVALIDARG;
        const auto id = nextToken_++;
        token->value = id;
        handler->AddRef();
        handlers_.push_back({id, handler});
        kinds_.push_back({id, kind});
        char line[160]; sprintf_s(line, "[ForgeShim] CoreWindow.add_%ls\n", kind); OutputDebugStringA(line);
        if (!wcscmp(kind, L"Activated") || !wcscmp(kind, L"VisibilityChanged")) PostMessageW(hwnd_, WM_APP + 0x45, 0, 0);
        return S_OK;
    }
public:
    // Invoke every handler registered for `kind` with (this window, args). All TypedEventHandler
    // instantiations share the layout Invoke(sender, args) at vtable slot 3.
    NativeDispatcher* Dispatcher() { return dispatcher_.Get(); }
    void Fire(const wchar_t* kind, void* args)
    {
        std::vector<IUnknown*> targets;
        for (auto& k : kinds_) if (!wcscmp(k.second, kind)) for (auto& h : handlers_) if (h.first == k.first) { h.second->AddRef(); targets.push_back(h.second); }
        for (auto* h : targets) {
            using InvokeFn = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, ICoreWindow*, void*);
            auto fn = reinterpret_cast<InvokeFn>((*reinterpret_cast<void***>(h))[3]);
            fn(h, static_cast<ICoreWindow*>(this), args); h->Release();
        }
    }
private:
    HRESULT Unregister(EventRegistrationToken token)
    {
        for (auto it = handlers_.begin(); it != handlers_.end(); ++it)
            if (it->first == token.value)
            { it->second->Release(); handlers_.erase(it); return S_OK; }
        return S_OK;
    }
public:
    ~NativeCoreWindow()
    { for (auto& h : handlers_) h.second->Release(); }
private:
    HWND hwnd_{};
    boolean inputEnabled_{true};
    ComPtr<NativeDispatcher> dispatcher_;
    int64_t nextToken_{1};
    std::vector<std::pair<int64_t, IUnknown*>> handlers_;
    std::vector<std::pair<int64_t, const wchar_t*>> kinds_;
};
NativeCoreWindow* g_nativeWindow = nullptr;

class KeyArgs final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IKeyEventArgs, ICoreWindowEventArgs>
{
    InspectableClass(L"Windows.UI.Core.KeyEventArgs", BaseTrust)
public:
    KeyArgs(UINT vk, CorePhysicalKeyStatus st) : vk_(vk), st_(st) {}
    HRESULT STDMETHODCALLTYPE get_VirtualKey(ABI::Windows::System::VirtualKey* v) override { *v = static_cast<ABI::Windows::System::VirtualKey>(vk_); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_KeyStatus(CorePhysicalKeyStatus* v) override { *v = st_; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* v) override { *v = handled_; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean v) override { handled_ = v; return S_OK; }
    boolean handled_ = false;
private:
    UINT vk_; CorePhysicalKeyStatus st_;
};
class CharArgs final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, ICharacterReceivedEventArgs, ICoreWindowEventArgs>
{
    InspectableClass(L"Windows.UI.Core.CharacterReceivedEventArgs", BaseTrust)
public:
    CharArgs(UINT code, CorePhysicalKeyStatus st) : code_(code), st_(st) {}
    HRESULT STDMETHODCALLTYPE get_KeyCode(UINT32* v) override { *v = code_; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_KeyStatus(CorePhysicalKeyStatus* v) override { *v = st_; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* v) override { *v = handled_; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean v) override { handled_ = v; return S_OK; }
    boolean handled_ = false;
private:
    UINT code_; CorePhysicalKeyStatus st_;
};

class ActivatedArgs final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IWindowActivatedEventArgs, ICoreWindowEventArgs>
{
    InspectableClass(L"Windows.UI.Core.WindowActivatedEventArgs", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE get_WindowActivationState(CoreWindowActivationState* v) override { *v = CoreWindowActivationState_CodeActivated; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* v) override { *v = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean) override { return S_OK; }
};
class VisibilityArgs final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IVisibilityChangedEventArgs, ICoreWindowEventArgs>
{
    InspectableClass(L"Windows.UI.Core.VisibilityChangedEventArgs", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE get_Visible(boolean* v) override { *v = true; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* v) override { *v = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean) override { return S_OK; }
};
// The game is always told it is visible and activated: background focus (and the PS5) must not pause it.
void FireActiveAndVisible()
{
    if (!g_nativeWindow) return;
    auto a = Make<ActivatedArgs>(); ComPtr<IWindowActivatedEventArgs> ai; a.As(&ai); g_nativeWindow->Fire(L"Activated", ai.Get());
    auto v = Make<VisibilityArgs>(); ComPtr<IVisibilityChangedEventArgs> vi; v.As(&vi); g_nativeWindow->Fire(L"VisibilityChanged", vi.Get());
    OutputDebugStringA("[ForgeShim] CoreWindow Activated(CodeActivated)+VisibilityChanged(true) fired\n");
}

class AccelArgs final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IAcceleratorKeyEventArgs, ICoreWindowEventArgs>
{
    InspectableClass(L"Windows.UI.Core.AcceleratorKeyEventArgs", BaseTrust)
public:
    AccelArgs(CoreAcceleratorKeyEventType t, UINT vk, CorePhysicalKeyStatus st) : t_(t), vk_(vk), st_(st) {}
    HRESULT STDMETHODCALLTYPE get_EventType(CoreAcceleratorKeyEventType* v) override { *v = t_; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_VirtualKey(ABI::Windows::System::VirtualKey* v) override { *v = static_cast<ABI::Windows::System::VirtualKey>(vk_); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_KeyStatus(CorePhysicalKeyStatus* v) override { *v = st_; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* v) override { *v = handled_; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean v) override { handled_ = v; return S_OK; }
    boolean handled_ = false;
private:
    CoreAcceleratorKeyEventType t_; UINT vk_; CorePhysicalKeyStatus st_;
};

bool ForwardPointer(UINT msg, WPARAM wp, LPARAM lp)
{
    if (!g_nativeWindow) return false;
    using namespace ABI::Windows::UI::Input;
    pointer::State st{};
    POINT pt{ static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)) };
    if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) ScreenToClient(g_hwnd, &pt);   // wheel coordinates are screen-relative
    st.pos = { float(pt.x), float(pt.y) };
    WPARAM keys = (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) ? GET_KEYSTATE_WPARAM(wp) : wp;
    st.left = (keys & MK_LBUTTON) != 0; st.right = (keys & MK_RBUTTON) != 0; st.middle = (keys & MK_MBUTTON) != 0;
    st.x1 = (keys & MK_XBUTTON1) != 0; st.x2 = (keys & MK_XBUTTON2) != 0;
    st.timestamp = GetTickCount64() * 1000; static UINT32 frame = 0; st.frame = ++frame;
    const wchar_t* kind = L"PointerMoved";
    switch (msg) {
    case WM_LBUTTONDOWN: st.kind = PointerUpdateKind_LeftButtonPressed; kind = L"PointerPressed"; break;
    case WM_LBUTTONUP: st.kind = PointerUpdateKind_LeftButtonReleased; kind = L"PointerReleased"; break;
    case WM_RBUTTONDOWN: st.kind = PointerUpdateKind_RightButtonPressed; kind = L"PointerPressed"; break;
    case WM_RBUTTONUP: st.kind = PointerUpdateKind_RightButtonReleased; kind = L"PointerReleased"; break;
    case WM_MBUTTONDOWN: st.kind = PointerUpdateKind_MiddleButtonPressed; kind = L"PointerPressed"; break;
    case WM_MBUTTONUP: st.kind = PointerUpdateKind_MiddleButtonReleased; kind = L"PointerReleased"; break;
    case WM_XBUTTONDOWN: st.kind = HIWORD(wp) == XBUTTON1 ? PointerUpdateKind_XButton1Pressed : PointerUpdateKind_XButton2Pressed; kind = L"PointerPressed"; break;
    case WM_XBUTTONUP: st.kind = HIWORD(wp) == XBUTTON1 ? PointerUpdateKind_XButton1Released : PointerUpdateKind_XButton2Released; kind = L"PointerReleased"; break;
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: st.wheel = GET_WHEEL_DELTA_WPARAM(wp); st.horizontal = msg == WM_MOUSEHWHEEL; kind = L"PointerWheelChanged"; break;
    default: break;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN) SetCapture(g_hwnd);
    if (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_MBUTTONUP) ReleaseCapture();
    auto a = Make<pointer::Args>(st); ComPtr<IPointerEventArgs> i; a.As(&i);
    g_nativeWindow->Fire(kind, i.Get());
    if (kind != std::wstring(L"PointerMoved")) { char line[96]; sprintf_s(line, "[ForgeShim] %ls at %ld,%ld\n", kind, pt.x, pt.y); OutputDebugStringA(line); }
    return a->handled_ != 0;
}

bool ForwardInput(UINT msg, WPARAM wp, LPARAM lp)
{
    if (!g_nativeWindow) return false;
    CorePhysicalKeyStatus st{};
    st.RepeatCount = static_cast<UINT32>(lp & 0xFFFF); st.ScanCode = static_cast<UINT32>((lp >> 16) & 0xFF);
    st.IsExtendedKey = (lp >> 24) & 1; st.IsMenuKeyDown = (lp >> 29) & 1; st.WasKeyDown = (lp >> 30) & 1; st.IsKeyReleased = (lp >> 31) & 1;
    if (auto* d = g_nativeWindow->Dispatcher(); d && d->AcceleratorCount()) {
        CoreAcceleratorKeyEventType t = msg == WM_KEYDOWN ? CoreAcceleratorKeyEventType_KeyDown : msg == WM_KEYUP ? CoreAcceleratorKeyEventType_KeyUp :
            msg == WM_SYSKEYDOWN ? CoreAcceleratorKeyEventType_SystemKeyDown : msg == WM_SYSKEYUP ? CoreAcceleratorKeyEventType_SystemKeyUp : CoreAcceleratorKeyEventType_Character;
        auto aa = Make<AccelArgs>(t, static_cast<UINT>(wp), st); ComPtr<IAcceleratorKeyEventArgs> ai; aa.As(&ai); d->FireAccelerator(ai.Get());
        if (aa->handled_) return true;
    }
    if (msg == WM_CHAR) { auto a = Make<CharArgs>(static_cast<UINT>(wp), st); ComPtr<ICharacterReceivedEventArgs> i; a.As(&i); g_nativeWindow->Fire(L"CharacterReceived", i.Get()); return a->handled_; }
    auto a = Make<KeyArgs>(static_cast<UINT>(wp), st); ComPtr<IKeyEventArgs> i; a.As(&i);
    bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    g_synthKeys[wp & 0xFF] = down ? 1 : 0;
    g_nativeWindow->Fire(down ? L"KeyDown" : L"KeyUp", i.Get());
    return a->handled_ != 0;
}

// FORGE_AUTOKEYS=<sec>-<hexVK>[.<sec>-<hexVK>...]: press keys at fixed times (test automation).
void StartAutoKeys(HWND hwnd)
{
    wchar_t spec[512]{}; if (!GetEnvironmentVariableW(L"FORGE_AUTOKEYS", spec, 512) || !spec[0]) return;
    std::wstring copy(spec);
    std::thread([hwnd, copy]() {
        ULONGLONG start = GetTickCount64(); size_t pos = 0;
        while (pos < copy.size()) {
            size_t dot = copy.find(L'.', pos); std::wstring item = copy.substr(pos, dot == std::wstring::npos ? std::wstring::npos : dot - pos);
            pos = dot == std::wstring::npos ? copy.size() : dot + 1;
            size_t dash = item.find(L'-'); if (dash == std::wstring::npos) continue;
            ULONGLONG at = _wtoi(item.substr(0, dash).c_str()) * 1000ULL; std::wstring what = item.substr(dash + 1);
            bool pad = !what.empty() && (what[0] == L'P' || what[0] == L'p'); UINT vk = wcstoul(what.c_str() + (pad ? 1 : 0), nullptr, 16);
            while (GetTickCount64() - start < at) Sleep(50);
            if (pad) {   // gamepad buttons (Windows.Gaming.Input.GamepadButtons bits), held 250 ms
                g_padSynth |= vk; Sleep(250); g_padSynth &= ~vk;
            } else if (vk == 0x01) {   // left click in the centre of the client area
                RECT rc{}; GetClientRect(hwnd, &rc); LPARAM at = MAKELPARAM((rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2);
                PostMessageW(hwnd, WM_MOUSEMOVE, 0, at); Sleep(50);
                PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, at); Sleep(150);
                PostMessageW(hwnd, WM_LBUTTONUP, 0, at);
            } else {
                UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
                PostMessageW(hwnd, WM_KEYDOWN, vk, 1 | (sc << 16)); Sleep(150);
                PostMessageW(hwnd, WM_KEYUP, vk, 1 | (sc << 16) | (1u << 30) | (1u << 31));
            }
            char line[96]; sprintf_s(line, "[ForgeShim] autokey %s%02X at %llus\n", pad ? "pad " : "vk=", vk, at / 1000); OutputDebugStringA(line);
        }
    }).detach();
}



// Bring the game window to the front for automated input (Windows refuses SetForegroundWindow from a background
// process; a synthetic ALT tap plus attaching to the foreground thread's input lifts that for this call).
static void ForceForeground(HWND hwnd)
{
    if (!hwnd || GetForegroundWindow() == hwnd) return;
    HWND fg = GetForegroundWindow(); DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, me = GetCurrentThreadId();
    INPUT alt[2]{}; alt[0].type = alt[1].type = INPUT_KEYBOARD; alt[0].ki.wVk = alt[1].ki.wVk = VK_MENU; alt[1].ki.dwFlags = KEYEVENTF_KEYUP; SendInput(2, alt, sizeof(INPUT));
    if (fgThread && fgThread != me) AttachThreadInput(me, fgThread, TRUE);
    ShowWindow(hwnd, SW_RESTORE); BringWindowToTop(hwnd); SetForegroundWindow(hwnd); SetFocus(hwnd);
    if (fgThread && fgThread != me) AttachThreadInput(me, fgThread, FALSE);
    char line[96]; sprintf_s(line, "[ForgeShim] livekeys: foreground %s\n", GetForegroundWindow() == hwnd ? "taken" : "still elsewhere"); OutputDebugStringA(line);
}
// FORGE_LIVEKEYS=1: test automation driven while the game runs. Lines appended to <Forge root>\h5campaign\livekeys.txt
// ("P<hex>" gamepad buttons held 250 ms, "W<ms>" wait, or a hex VK) are pressed in order as they arrive.
void StartLiveKeys(HWND hwnd)
{
    if (!GetEnvironmentVariableW(L"FORGE_LIVEKEYS", nullptr, 0)) return;
    std::thread([hwnd]() {
        wchar_t path[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, path); wcscat_s(path, L"\\h5campaign\\livekeys.txt");
        long long done = -1; std::string pending;
        for (;;) {
            Sleep(200);
            HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (f == INVALID_HANDLE_VALUE) continue;
            LARGE_INTEGER size{}; GetFileSizeEx(f, &size);
            if (done < 0) done = size.QuadPart;   // ignore whatever was there before this run
            if (size.QuadPart > done) {
                std::string chunk(static_cast<size_t>(size.QuadPart - done), '\0'); LARGE_INTEGER at{}; at.QuadPart = done; SetFilePointerEx(f, at, nullptr, FILE_BEGIN);
                DWORD got = 0; ReadFile(f, chunk.data(), static_cast<DWORD>(chunk.size()), &got, nullptr); chunk.resize(got); done += got; pending += chunk;
            }
            CloseHandle(f);
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                std::string item = pending.substr(0, nl); pending.erase(0, nl + 1);
                while (!item.empty() && (item.back() == '\r' || item.back() == ' ')) item.pop_back();
                if (item.empty()) continue;
                ForceForeground(hwnd);
                if (item[0] == 'W' || item[0] == 'w') { Sleep(strtoul(item.c_str() + 1, nullptr, 10)); continue; }
                if (item[0] == 'M' || item[0] == 'm') { ::CampaignSelectMission(static_cast<int>(strtol(item.c_str() + 1, nullptr, 10))); continue; }
                bool pad = item[0] == 'P' || item[0] == 'p'; UINT vk = strtoul(item.c_str() + (pad ? 1 : 0), nullptr, 16);
                if (pad) { g_padSynth |= vk; Sleep(250); g_padSynth &= ~vk; Sleep(150); }
                else { UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC); PostMessageW(hwnd, WM_KEYDOWN, vk, 1 | (sc << 16)); Sleep(150); PostMessageW(hwnd, WM_KEYUP, vk, 1 | (sc << 16) | (1u << 30) | (1u << 31)); }
                char line[96]; sprintf_s(line, "[ForgeShim] livekey %s%02X\n", pad ? "pad " : "vk=", vk); OutputDebugStringA(line);
            }
        }
    }).detach();
}

class NativeCoreWindowStatics final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, ICoreWindowStatic, IActivationFactory>
{
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override{if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetForCurrentThread(ICoreWindow** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        ComPtr<ICoreWindow> current;
        HRESULT hr = GetCurrentWindow(current.GetAddressOf());
        if (FAILED(hr)) return hr;
        // Return the owned ICoreWindow interface pointer directly.
        ICoreWindow* ptr = current.Detach();
        char info[192]{};
        sprintf_s(info, "[H5S11] CoreWindow.GetForCurrentThread tid=%lu object=%p\n",
                  static_cast<unsigned long>(GetCurrentThreadId()), static_cast<void*>(ptr));
        /* verbose window identity trace suppressed */
        *out = ptr;
        return S_OK;
    }
};

// Minimal launch-activation event object, including launch argument queries.
class NativeActivatedEventArgs final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,
    ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs,
    ABI::Windows::ApplicationModel::Activation::ILaunchActivatedEventArgs,ABI::Windows::ApplicationModel::Activation::IPrelaunchActivatedEventArgs>
{
public:
    HRESULT STDMETHODCALLTYPE get_PrelaunchActivated(boolean* out) override{if(!out)return E_POINTER;*out=false;return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {using Base=RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs,ABI::Windows::ApplicationModel::Activation::ILaunchActivatedEventArgs,ABI::Windows::ApplicationModel::Activation::IPrelaunchActivatedEventArgs>;HRESULT hr=Base::QueryInterface(iid,out);wchar_t id[64],line[160];StringFromGUID2(iid,id,64);swprintf_s(line,L"[ForgeShim] ActivatedArgs QI %s hr=%08X\n",id,hr);if(FAILED(hr))OutputDebugStringW(line);return hr;}
    HRESULT STDMETHODCALLTYPE get_Kind(ABI::Windows::ApplicationModel::Activation::ActivationKind* out) override
    {
        if (!out) return E_POINTER;
        *out = static_cast<ABI::Windows::ApplicationModel::Activation::ActivationKind>(0); // Launch
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PreviousExecutionState(
        ABI::Windows::ApplicationModel::Activation::ApplicationExecutionState* out) override
    {
        if (!out) return E_POINTER;
        *out = static_cast<ABI::Windows::ApplicationModel::Activation::ApplicationExecutionState>(0); // NotRunning
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SplashScreen(
        ABI::Windows::ApplicationModel::Activation::ISplashScreen** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Arguments(HSTRING* out) override
    {
        if (!out) return E_POINTER;
        return WindowsCreateString(L"", 0, out);
    }
    HRESULT STDMETHODCALLTYPE get_TileId(HSTRING* out) override
    {
        if (!out) return E_POINTER;
        return WindowsCreateString(L"", 0, out);
    }
};

class NativeApplicationView final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,
    ICoreApplicationView, ICoreApplicationView2>
{
public:
    explicit NativeApplicationView(ComPtr<ICoreWindow> window) : window_(std::move(window)) {}
    HRESULT STDMETHODCALLTYPE get_CoreWindow(ICoreWindow** v) override { return window_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE add_Activated(
        ITypedEventHandler<CoreApplicationView*, ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs*>* handler,
        EventRegistrationToken* token) override
    {
        if (!handler || !token) return E_INVALIDARG;
        token->value = ++nextToken_;
        activated_ = handler;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE remove_Activated(EventRegistrationToken token) override
    { if (token.value == nextToken_) activated_.Reset(); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsMain(boolean* v) override
    { if (!v) return E_POINTER; *v = true; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsHosted(boolean* v) override
    { if (!v) return E_POINTER; *v = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Dispatcher(ICoreDispatcher** v) override { return window_->get_Dispatcher(v); }
    HRESULT FireActivation()
    {
        if (!activated_) { Trace("[H5S09] No Activated handler registered\n"); return S_OK; }
        auto args = Make<NativeActivatedEventArgs>();
        if (!args) return E_OUTOFMEMORY;
        Trace("[H5S09] Invoking CoreApplicationView.Activated handler\n");
        ICoreApplicationView* viewAbi = static_cast<ICoreApplicationView*>(this);
        ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs* argsAbi =
            static_cast<ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs*>(args.Get());
        return activated_.Get()->Invoke(viewAbi, argsAbi);
    }
private:
    ComPtr<ICoreWindow> window_;
    ComPtr<ITypedEventHandler<CoreApplicationView*, ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs*>> activated_;
    int64_t nextToken_{};
};

} // anonymous namespace

HWND WindowHandle() noexcept { return g_hwnd; }
bool IsFullscreen() noexcept { return g_fullscreen; }
void RequestFullscreen(bool on) noexcept { if (g_hwnd) PostMessageW(g_hwnd, FullscreenMessage, on ? 1 : 0, 0); }

HRESULT GetCoreWindowStaticFactory(REFIID iid, void** out) noexcept
{
    if (!out) return E_POINTER;
    *out = nullptr;
    auto factory = Make<NativeCoreWindowStatics>();
    if (!factory) return E_OUTOFMEMORY;
    return factory.CopyTo(iid, out);
}

bool Enabled() noexcept
{
    wchar_t flag[8]{};
    return GetEnvironmentVariableW(L"HALO5_NATIVE_HOST", flag, ARRAYSIZE(flag)) > 0 && flag[0] == L'1';
}

HRESULT GetCurrentWindow(ICoreWindow** out) noexcept { return g_window.CopyTo(out); }
HRESULT GetCurrentView(ICoreApplicationView** out) noexcept { return g_view.CopyTo(out); }

HRESULT Run(IFrameworkViewSource* source) noexcept
{
    if (!source) return E_POINTER;
    if (GetCurrentThreadId() != g_uiThread && g_uiThread != 0) return RPC_E_WRONG_THREAD;
    g_uiThread = GetCurrentThreadId();
    g_quit = false;
    Trace("[H5S09] Entering unpackaged desktop host\n");

    // Real Win32 HWND rather than a packaged CoreWindow provided by Windows.
    static const wchar_t* cls = L"Halo5CampaignWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    RECT r{0, 0, 1280, 720};
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    g_hwnd = CreateWindowExW(0, cls, L"Osiris",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right-r.left, r.bottom-r.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd) return HRESULT_FROM_WIN32(GetLastError());
    {
        wchar_t mode[16]{}; GetEnvironmentVariableW(L"FORGE_WINDOW", mode, 16);
        ApplyFullscreen(g_hwnd, _wcsicmp(mode, L"windowed") != 0);   // before the game creates its swap chain
    }
    g_dispatchQueue=std::make_shared<NativeDispatchState>(g_hwnd);
    Trace("[H5S09] Created native HWND\n");
    struct Cleanup
    {
        ~Cleanup()
        {
            g_window.Reset();
            g_view.Reset();
            if (g_hwnd && IsWindow(g_hwnd)) DestroyWindow(g_hwnd);
            g_hwnd = nullptr;
            g_uiThread = 0;
        }
    } cleanup;

    auto windowImpl = Make<NativeCoreWindow>(g_hwnd);
    g_nativeWindow = windowImpl.Get();
    StartAutoKeys(g_hwnd); StartLiveKeys(g_hwnd); ::StartExitCombo(g_hwnd);
    if (!windowImpl) return E_OUTOFMEMORY;
    HRESULT hr = windowImpl.As(&g_window);
    if (FAILED(hr)) return hr;
    {
        char info[192]{};
        sprintf_s(info, "[H5S11] Native CoreWindow created tid=%lu object=%p hwnd=%p\n",
                  static_cast<unsigned long>(GetCurrentThreadId()),
                  static_cast<void*>(g_window.Get()), static_cast<void*>(g_hwnd));
        /* verbose window identity trace suppressed */
    }
    auto viewImpl = Make<NativeApplicationView>(g_window);
    if (!viewImpl) return E_OUTOFMEMORY;
    hr = viewImpl.As(&g_view);
    if (FAILED(hr)) return hr;

    ComPtr<IFrameworkView> gameView;
    hr = source->CreateView(gameView.GetAddressOf());
    TraceHr("CreateView", hr);
    if (FAILED(hr)) return hr;
    if (!gameView) return E_UNEXPECTED;
    hr = gameView->Initialize(g_view.Get());
    TraceHr("Initialize", hr);
    if (FAILED(hr)) return hr;
    hr = gameView->SetWindow(g_window.Get());
    TraceHr("SetWindow", hr);
    if (FAILED(hr)) { gameView->Uninitialize(); return hr; }
    hr = g_window->Activate();
    TraceHr("Activate", hr);
    if (FAILED(hr)) { gameView->Uninitialize(); return hr; }
    // Use Halo 5's manifest entry point, with an override for other dumps.
    wchar_t overrideName[256]{};
    DWORD overrideLen = GetEnvironmentVariableW(L"HALO5_ENTRYPOINT", overrideName, ARRAYSIZE(overrideName));
    const wchar_t* entryName = (overrideLen > 0 && overrideLen < ARRAYSIZE(overrideName))
        ? overrideName : L"Osiris.App";
    HSTRING entry{};
    hr = WindowsCreateString(entryName, static_cast<UINT32>(lstrlenW(entryName)), &entry);
    if (SUCCEEDED(hr))
    {
        hr = gameView->Load(entry);
        WindowsDeleteString(entry);
    }
    TraceHr("Load", hr);
    if (SUCCEEDED(hr))
    {
        hr = viewImpl->FireActivation();
        TraceHr("FireActivation", hr);
    }
    if (SUCCEEDED(hr))
    {
        Trace("[H5S09] Calling IFrameworkView::Run\n");
        hr = gameView->Run();
        TraceHr("Run", hr);
    }
    const HRESULT uninit = gameView->Uninitialize();
    TraceHr("Uninitialize", uninit);
    return hr;
}
} // namespace h5::desktop



