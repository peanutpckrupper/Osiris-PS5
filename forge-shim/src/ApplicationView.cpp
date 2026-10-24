#include "NativeDesktopHost.h"
#include <windows.ui.viewmanagement.h>
#include <wrl.h>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::ViewManagement;
using namespace ABI::Windows::Foundation;
namespace h5::desktop { HWND WindowHandle() noexcept; bool IsFullscreen() noexcept; void RequestFullscreen(bool) noexcept; }
class DesktopApplicationView final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IApplicationView,IApplicationView2,IApplicationView3>{
 ApplicationViewBoundsMode bounds{};boolean capture=true,overlays=false;INT64 next=0;
public:
 HRESULT STDMETHODCALLTYPE get_TitleBar(IApplicationViewTitleBar** out) override{if(out)*out=nullptr;OutputDebugStringA("[ForgeShim] unsupported ApplicationView.TitleBar\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE get_FullScreenSystemOverlayMode(FullScreenSystemOverlayMode* out) override{if(!out)return E_POINTER;*out=static_cast<FullScreenSystemOverlayMode>(0);return S_OK;}
 HRESULT STDMETHODCALLTYPE put_FullScreenSystemOverlayMode(FullScreenSystemOverlayMode) override{return S_OK;}
 HRESULT STDMETHODCALLTYPE get_IsFullScreenMode(boolean* out) override{if(!out)return E_POINTER;*out=h5::desktop::IsFullscreen();return S_OK;}
 HRESULT STDMETHODCALLTYPE TryEnterFullScreenMode(boolean* out) override{if(!out)return E_POINTER;h5::desktop::RequestFullscreen(true);*out=true;OutputDebugStringA("[ForgeShim] ApplicationView: enter fullscreen\n");return S_OK;}
 HRESULT STDMETHODCALLTYPE ExitFullScreenMode() override{h5::desktop::RequestFullscreen(false);OutputDebugStringA("[ForgeShim] ApplicationView: exit fullscreen\n");return S_OK;}
 HRESULT STDMETHODCALLTYPE ShowStandardSystemOverlays() override{return S_OK;}
 HRESULT STDMETHODCALLTYPE TryResizeView(Size size,boolean* out) override{if(!out)return E_POINTER;RECT r{0,0,LONG(size.Width),LONG(size.Height)};auto h=h5::desktop::WindowHandle();AdjustWindowRectEx(&r,static_cast<DWORD>(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,0);*out=static_cast<boolean>(SetWindowPos(h,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER)!=0);return S_OK;}
 HRESULT STDMETHODCALLTYPE SetPreferredMinSize(Size) override{OutputDebugStringA("[ForgeShim] ApplicationView.SetPreferredMinSize\n");return S_OK;}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override{using Base=RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IApplicationView,IApplicationView2,IApplicationView3>;HRESULT hr=Base::QueryInterface(iid,out);wchar_t id[64],line[160];StringFromGUID2(iid,id,64);swprintf_s(line,L"[ForgeShim] ApplicationView QI %s hr=%08X\n",id,hr);OutputDebugStringW(line);return hr;}
 HRESULT STDMETHODCALLTYPE get_Orientation(ApplicationViewOrientation* out) override{if(!out)return E_POINTER;*out=ApplicationViewOrientation_Landscape;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_AdjacentToLeftDisplayEdge(boolean* out) override{if(!out)return E_POINTER;*out=false;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_AdjacentToRightDisplayEdge(boolean* out) override{return get_AdjacentToLeftDisplayEdge(out);}
 HRESULT STDMETHODCALLTYPE get_IsFullScreen(boolean* out) override{return get_AdjacentToLeftDisplayEdge(out);}
 HRESULT STDMETHODCALLTYPE get_IsOnLockScreen(boolean* out) override{return get_AdjacentToLeftDisplayEdge(out);}
 HRESULT STDMETHODCALLTYPE get_IsScreenCaptureEnabled(boolean* out) override{if(!out)return E_POINTER;*out=capture;return S_OK;}
 HRESULT STDMETHODCALLTYPE put_IsScreenCaptureEnabled(boolean value) override{capture=value;return S_OK;}
 HRESULT STDMETHODCALLTYPE put_Title(HSTRING text) override{return SetWindowTextW(h5::desktop::WindowHandle(),WindowsGetStringRawBuffer(text,nullptr))?S_OK:E_FAIL;}
 HRESULT STDMETHODCALLTYPE get_Title(HSTRING* out) override{wchar_t title[512];int n=GetWindowTextW(h5::desktop::WindowHandle(),title,512);return WindowsCreateString(title,n,out);}
 HRESULT STDMETHODCALLTYPE get_Id(INT32* out) override{if(!out)return E_POINTER;*out=1;return S_OK;}
 HRESULT STDMETHODCALLTYPE add_Consolidated(__FITypedEventHandler_2_Windows__CUI__CViewManagement__CApplicationView_Windows__CUI__CViewManagement__CApplicationViewConsolidatedEventArgs*,EventRegistrationToken* t) override{if(!t)return E_POINTER;t->value=++next;return S_OK;}
 HRESULT STDMETHODCALLTYPE remove_Consolidated(EventRegistrationToken) override{return S_OK;}
 HRESULT STDMETHODCALLTYPE get_SuppressSystemOverlays(boolean* out) override{if(!out)return E_POINTER;*out=overlays;return S_OK;}
 HRESULT STDMETHODCALLTYPE put_SuppressSystemOverlays(boolean value) override{overlays=value;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_VisibleBounds(Rect* out) override{if(!out)return E_POINTER;RECT r{};GetClientRect(h5::desktop::WindowHandle(),&r);*out={0,0,float(r.right),float(r.bottom)};return S_OK;}
 HRESULT STDMETHODCALLTYPE add_VisibleBoundsChanged(__FITypedEventHandler_2_Windows__CUI__CViewManagement__CApplicationView_IInspectable*,EventRegistrationToken* t) override{if(!t)return E_POINTER;t->value=++next;return S_OK;}
 HRESULT STDMETHODCALLTYPE remove_VisibleBoundsChanged(EventRegistrationToken) override{return S_OK;}
 HRESULT STDMETHODCALLTYPE SetDesiredBoundsMode(ApplicationViewBoundsMode mode,boolean* out) override{if(!out)return E_POINTER;OutputDebugStringA("[ForgeShim] ApplicationView.SetDesiredBoundsMode\n");bounds=mode;*out=true;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_DesiredBoundsMode(ApplicationViewBoundsMode* out) override{if(!out)return E_POINTER;*out=bounds;return S_OK;}
};
class DesktopApplicationViewFactory final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IActivationFactory,IApplicationViewStatics2,IApplicationViewStatics3>{
 ComPtr<DesktopApplicationView> view=Make<DesktopApplicationView>();boolean terminate=true;
 ApplicationViewWindowingMode mode=ApplicationViewWindowingMode_PreferredLaunchViewSize;Size preferred{1280,720};
public:
 HRESULT STDMETHODCALLTYPE get_PreferredLaunchWindowingMode(ApplicationViewWindowingMode* out) override{if(!out)return E_POINTER;*out=mode;return S_OK;}
 HRESULT STDMETHODCALLTYPE put_PreferredLaunchWindowingMode(ApplicationViewWindowingMode value) override{mode=value;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_PreferredLaunchViewSize(Size* out) override{if(!out)return E_POINTER;*out=preferred;return S_OK;}
 HRESULT STDMETHODCALLTYPE put_PreferredLaunchViewSize(Size value) override{preferred=value;return S_OK;}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override{using Base=RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IActivationFactory,IApplicationViewStatics2,IApplicationViewStatics3>;HRESULT hr=Base::QueryInterface(iid,out);wchar_t id[64],line[160];StringFromGUID2(iid,id,64);swprintf_s(line,L"[ForgeShim] ApplicationViewFactory QI %s hr=%08X\n",id,hr);OutputDebugStringW(line);return hr;}
 HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override{if(out)*out=nullptr;return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetForCurrentView(IApplicationView** out) override{OutputDebugStringA("[ForgeShim] ApplicationView.GetForCurrentView\n");return view.CopyTo(out);}
 HRESULT STDMETHODCALLTYPE get_TerminateAppOnFinalViewClose(boolean* out) override{if(!out)return E_POINTER;*out=terminate;return S_OK;}
 HRESULT STDMETHODCALLTYPE put_TerminateAppOnFinalViewClose(boolean value) override{terminate=value;return S_OK;}
};
HRESULT ApplicationViewActivation(REFIID iid,void** out){return Make<DesktopApplicationViewFactory>().CopyTo(iid,out);}


