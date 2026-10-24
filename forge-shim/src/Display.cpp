#include <windows.h>
#include <windows.graphics.display.h>
#include <wrl.h>
#include <vector>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Graphics::Display;
using namespace ABI::Windows::Foundation;
using Handler=ITypedEventHandler<DisplayInformation*,IInspectable*>;
class DesktopDisplay final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IDisplayInformation,IDisplayInformation2,IDisplayInformation4>{
 std::vector<ComPtr<Handler>> handlers;
public:
 HRESULT STDMETHODCALLTYPE get_ScreenWidthInRawPixels(UINT32* out) override{if(!out)return E_POINTER;*out=GetSystemMetrics(SM_CXSCREEN);return S_OK;}
 HRESULT STDMETHODCALLTYPE get_ScreenHeightInRawPixels(UINT32* out) override{if(!out)return E_POINTER;*out=GetSystemMetrics(SM_CYSCREEN);return S_OK;}
 HRESULT STDMETHODCALLTYPE get_RawPixelsPerViewPixel(DOUBLE* out) override{if(!out)return E_POINTER;*out=1.0;return S_OK;}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override{using Base=RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IDisplayInformation,IDisplayInformation2,IDisplayInformation4>;HRESULT hr=Base::QueryInterface(iid,out);wchar_t id[64],line[160];StringFromGUID2(iid,id,64);swprintf_s(line,L"[ForgeShim] Display QI %s hr=%08X\n",id,hr);OutputDebugStringW(line);return hr;}
 HRESULT STDMETHODCALLTYPE get_CurrentOrientation(DisplayOrientations* out) override{if(!out)return E_POINTER;*out=DisplayOrientations_Landscape;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_NativeOrientation(DisplayOrientations* out) override{return get_CurrentOrientation(out);}
 HRESULT STDMETHODCALLTYPE get_ResolutionScale(ResolutionScale* out) override{if(!out)return E_POINTER;*out=ResolutionScale_Scale100Percent;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_LogicalDpi(FLOAT* out) override{if(!out)return E_POINTER;*out=96;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_RawDpiX(FLOAT* out) override{return get_LogicalDpi(out);}
 HRESULT STDMETHODCALLTYPE get_RawDpiY(FLOAT* out) override{return get_LogicalDpi(out);}
 HRESULT STDMETHODCALLTYPE get_StereoEnabled(boolean* out) override{if(!out)return E_POINTER;*out=false;return S_OK;}
 HRESULT STDMETHODCALLTYPE GetColorProfileAsync(__FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStream** out) override{if(out)*out=nullptr;OutputDebugStringA("[ForgeShim] unsupported Display.ColorProfile\n");return E_NOTIMPL;}
#define EVENT(name) HRESULT STDMETHODCALLTYPE add_##name(Handler* h,EventRegistrationToken* t) override{if(!h||!t)return E_POINTER;handlers.emplace_back(h);t->value=handlers.size();return S_OK;} HRESULT STDMETHODCALLTYPE remove_##name(EventRegistrationToken t) override{if(t.value>0&&static_cast<size_t>(t.value)<=handlers.size())handlers[t.value-1].Reset();return S_OK;}
 EVENT(OrientationChanged) EVENT(DpiChanged) EVENT(StereoEnabledChanged) EVENT(ColorProfileChanged)
#undef EVENT
};
class DisplayFactory final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IActivationFactory,IDisplayInformationStatics>{
 ComPtr<DesktopDisplay> display=Make<DesktopDisplay>();DisplayOrientations rotation=DisplayOrientations_Landscape;std::vector<ComPtr<Handler>> handlers;
public:
 HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override{if(out)*out=nullptr;return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetForCurrentView(IDisplayInformation** out) override{OutputDebugStringA("[ForgeShim] DisplayInformation.GetForCurrentView\n");return display.CopyTo(out);}
 HRESULT STDMETHODCALLTYPE get_AutoRotationPreferences(DisplayOrientations* out) override{if(!out)return E_POINTER;*out=rotation;return S_OK;}
 HRESULT STDMETHODCALLTYPE put_AutoRotationPreferences(DisplayOrientations value) override{rotation=value;return S_OK;}
 HRESULT STDMETHODCALLTYPE add_DisplayContentsInvalidated(Handler* h,EventRegistrationToken* t) override{if(!h||!t)return E_POINTER;handlers.emplace_back(h);t->value=handlers.size();return S_OK;}
 HRESULT STDMETHODCALLTYPE remove_DisplayContentsInvalidated(EventRegistrationToken t) override{if(t.value>0&&static_cast<size_t>(t.value)<=handlers.size())handlers[t.value-1].Reset();return S_OK;}
};
HRESULT DisplayActivation(REFIID iid,void** out){return Make<DisplayFactory>().CopyTo(iid,out);}


