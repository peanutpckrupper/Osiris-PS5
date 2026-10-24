#include <windows.h>
#include <windows.devices.input.h>
#include <wrl.h>
#include <vector>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Devices::Input;
using namespace ABI::Windows::Foundation;
class DesktopMouse final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IMouseDevice>{
 std::vector<ComPtr<ITypedEventHandler<MouseDevice*,MouseEventArgs*>>> handlers;
public:
 HRESULT STDMETHODCALLTYPE add_MouseMoved(ITypedEventHandler<MouseDevice*,MouseEventArgs*>* h,EventRegistrationToken* t) override{if(!h||!t)return E_POINTER;handlers.emplace_back(h);t->value=handlers.size();OutputDebugStringA("[ForgeShim] MouseDevice.MouseMoved subscribed\n");return S_OK;}
 HRESULT STDMETHODCALLTYPE remove_MouseMoved(EventRegistrationToken t) override{if(t.value>0&&static_cast<size_t>(t.value)<=handlers.size())handlers[t.value-1].Reset();return S_OK;}
};
class MouseFactory final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IActivationFactory,IMouseDeviceStatics>{
 ComPtr<DesktopMouse> mouse=Make<DesktopMouse>();
public:
 HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override{if(out)*out=nullptr;return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetForCurrentView(IMouseDevice** out) override{OutputDebugStringA("[ForgeShim] MouseDevice.GetForCurrentView\n");return mouse.CopyTo(out);}
};
HRESULT MouseActivation(REFIID iid,void** out){return Make<MouseFactory>().CopyTo(iid,out);}
