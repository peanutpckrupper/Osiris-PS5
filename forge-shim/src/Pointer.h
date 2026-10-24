// Pointer (mouse) event arguments for the desktop CoreWindow: PointerPressed/Released/Moved/WheelChanged.
#pragma once
#include <windows.h>
#include <wrl.h>
#include <windows.ui.core.h>
#include <windows.ui.input.h>
#include <windows.devices.input.h>
#include <windows.foundation.collections.h>

namespace h5::desktop::pointer {
using namespace Microsoft::WRL;
using ABI::Windows::Foundation::Point;
using ABI::Windows::Foundation::Rect;

struct State { Point pos{}; bool left = false, right = false, middle = false, x1 = false, x2 = false; int wheel = 0; bool horizontal = false;
               ABI::Windows::UI::Input::PointerUpdateKind kind = ABI::Windows::UI::Input::PointerUpdateKind_Other; UINT64 timestamp = 0; UINT32 frame = 0; };

class Device final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, ABI::Windows::Devices::Input::IPointerDevice> {
    InspectableClass(L"Windows.Devices.Input.PointerDevice", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE get_PointerDeviceType(ABI::Windows::Devices::Input::PointerDeviceType* v) override { *v = ABI::Windows::Devices::Input::PointerDeviceType_Mouse; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsIntegrated(boolean* v) override { *v = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_MaxContacts(UINT32* v) override { *v = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_PhysicalDeviceRect(Rect* v) override { *v = Rect{}; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ScreenRect(Rect* v) override { *v = Rect{0, 0, float(GetSystemMetrics(SM_CXSCREEN)), float(GetSystemMetrics(SM_CYSCREEN))}; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_SupportedUsages(ABI::Windows::Foundation::Collections::IVectorView<ABI::Windows::Devices::Input::PointerDeviceUsage>** v) override { if (v) *v = nullptr; return E_NOTIMPL; }
};

class Properties final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, ABI::Windows::UI::Input::IPointerPointProperties> {
    InspectableClass(L"Windows.UI.Input.PointerPointProperties", BaseTrust)
    State s_;
public:
    explicit Properties(const State& s) : s_(s) {}
    HRESULT STDMETHODCALLTYPE get_Pressure(FLOAT* v) override { *v = (s_.left || s_.right || s_.middle) ? 0.5f : 0.f; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsInverted(boolean* v) override { *v = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsEraser(boolean* v) override { *v = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Orientation(FLOAT* v) override { *v = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_XTilt(FLOAT* v) override { *v = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_YTilt(FLOAT* v) override { *v = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Twist(FLOAT* v) override { *v = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ContactRect(Rect* v) override { *v = Rect{s_.pos.X, s_.pos.Y, 1, 1}; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ContactRectRaw(Rect* v) override { *v = Rect{s_.pos.X, s_.pos.Y, 1, 1}; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_TouchConfidence(boolean* v) override { *v = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsLeftButtonPressed(boolean* v) override { *v = s_.left; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsRightButtonPressed(boolean* v) override { *v = s_.right; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsMiddleButtonPressed(boolean* v) override { *v = s_.middle; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_MouseWheelDelta(INT32* v) override { *v = s_.wheel; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsHorizontalMouseWheel(boolean* v) override { *v = s_.horizontal; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsPrimary(boolean* v) override { *v = true; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsInRange(boolean* v) override { *v = true; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsCanceled(boolean* v) override { *v = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsBarrelButtonPressed(boolean* v) override { *v = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsXButton1Pressed(boolean* v) override { *v = s_.x1; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsXButton2Pressed(boolean* v) override { *v = s_.x2; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_PointerUpdateKind(ABI::Windows::UI::Input::PointerUpdateKind* v) override { *v = s_.kind; return S_OK; }
    HRESULT STDMETHODCALLTYPE HasUsage(UINT32, UINT32, boolean* v) override { *v = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetUsageValue(UINT32, UINT32, INT32* v) override { *v = 0; return S_OK; }
};

class PointPoint final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, ABI::Windows::UI::Input::IPointerPoint> {
    InspectableClass(L"Windows.UI.Input.PointerPoint", BaseTrust)
    State s_;
public:
    explicit PointPoint(const State& s) : s_(s) {}
    HRESULT STDMETHODCALLTYPE get_PointerDevice(ABI::Windows::Devices::Input::IPointerDevice** v) override { auto d = Make<Device>(); return d.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_Position(Point* v) override { *v = s_.pos; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_RawPosition(Point* v) override { *v = s_.pos; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_PointerId(UINT32* v) override { *v = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_FrameId(UINT32* v) override { *v = s_.frame; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Timestamp(UINT64* v) override { *v = s_.timestamp; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsInContact(boolean* v) override { *v = s_.left || s_.right || s_.middle; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Properties(ABI::Windows::UI::Input::IPointerPointProperties** v) override { auto p = Make<Properties>(s_); return p.CopyTo(v); }
};

class Args final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, ABI::Windows::UI::Core::IPointerEventArgs, ABI::Windows::UI::Core::ICoreWindowEventArgs> {
    InspectableClass(L"Windows.UI.Core.PointerEventArgs", BaseTrust)
    State s_;
public:
    explicit Args(const State& s) : s_(s) {}
    boolean handled_ = false;
    HRESULT STDMETHODCALLTYPE get_CurrentPoint(ABI::Windows::UI::Input::IPointerPoint** v) override { auto p = Make<PointPoint>(s_); return p.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_KeyModifiers(ABI::Windows::System::VirtualKeyModifiers* v) override {
        int m = 0; if (GetKeyState(VK_CONTROL) < 0) m |= 1; if (GetKeyState(VK_MENU) < 0) m |= 2; if (GetKeyState(VK_SHIFT) < 0) m |= 4;
        *v = static_cast<ABI::Windows::System::VirtualKeyModifiers>(m); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetIntermediatePoints(ABI::Windows::Foundation::Collections::IVector<ABI::Windows::UI::Input::PointerPoint*>** v) override { if (v) *v = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* v) override { *v = handled_; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean v) override { handled_ = v; return S_OK; }
};
}
