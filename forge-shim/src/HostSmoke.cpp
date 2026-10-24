#include "NativeDesktopHost.h"
#include <CoreWindow.h>
#include <roapi.h>
#include <wrl.h>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::Foundation;
class TestView final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IFrameworkView>{
 int step=0;bool window=false;
public:
 bool completed=false;
 HRESULT STDMETHODCALLTYPE Initialize(ICoreApplicationView* v) override{if(step++!=0||!v)return E_FAIL;return S_OK;}
 HRESULT STDMETHODCALLTYPE SetWindow(ICoreWindow* w) override{if(step++!=1||!w)return E_FAIL;ComPtr<ICoreWindowInterop> interop;HRESULT hr=w->QueryInterface(IID_PPV_ARGS(&interop));if(FAILED(hr))return hr;HWND h=nullptr;hr=interop->get_WindowHandle(&h);if(FAILED(hr)||!IsWindow(h))return E_FAIL;Rect r{};if(FAILED(w->get_Bounds(&r))||r.Width!=1280||r.Height!=720)return E_FAIL;ComPtr<ICoreDispatcher> d;boolean access=false;if(FAILED(w->get_Dispatcher(&d))||FAILED(d->get_HasThreadAccess(&access))||!access)return E_FAIL;window=true;return S_OK;}
 HRESULT STDMETHODCALLTYPE Load(HSTRING entry) override{if(step++!=2||wcscmp(WindowsGetStringRawBuffer(entry,nullptr),L"Osiris.App"))return E_FAIL;return S_OK;}
 HRESULT STDMETHODCALLTYPE Run() override{return step++==3&&window?S_OK:E_FAIL;}
 HRESULT STDMETHODCALLTYPE Uninitialize() override{completed=step++==4;return completed?S_OK:E_FAIL;}
};
class TestSource final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IFrameworkViewSource>{
public:ComPtr<TestView> view=Make<TestView>();HRESULT STDMETHODCALLTYPE CreateView(IFrameworkView** out) override{return view.CopyTo(out);}
};
int main(){HRESULT init=RoInitialize(RO_INIT_SINGLETHREADED);auto source=Make<TestSource>();HRESULT hr=h5::desktop::Run(source.Get());bool pass=SUCCEEDED(hr)&&source->view->completed;printf("desktop host lifecycle/real HWND/bounds/UI-thread: %s hr=%08X\n",pass?"PASS":"FAIL",hr);if(SUCCEEDED(init))RoUninitialize();return pass?0:1;}
