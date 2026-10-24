#include <windows.h>
#include <windows.security.exchangeactivesyncprovisioning.h>
#include <wrl.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Security::ExchangeActiveSyncProvisioning;
static HRESULT Text(const wchar_t* s,HSTRING* out){return WindowsCreateString(s,static_cast<UINT32>(wcslen(s)),out);}
class DeviceInfo final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IEasClientDeviceInformation>{
public:
 HRESULT STDMETHODCALLTYPE get_Id(GUID* out) override{if(!out)return E_POINTER;*out={0x48414c4f,0x3546,0x4f52,{0x87,0x45,0x53,0x54,0x41,0x4e,0x44,0x31}};return S_OK;}
 HRESULT STDMETHODCALLTYPE get_OperatingSystem(HSTRING* out) override{return Text(L"WINDOWS",out);}
 HRESULT STDMETHODCALLTYPE get_FriendlyName(HSTRING* out) override{OutputDebugStringA("[ForgeShim] DeviceInfo.FriendlyName\n");wchar_t name[256];DWORD n=256;if(!GetComputerNameW(name,&n))return Text(L"ForgeStandalone",out);return WindowsCreateString(name,n,out);}
 HRESULT STDMETHODCALLTYPE get_SystemManufacturer(HSTRING* out) override{return Text(L"Desktop",out);}
 HRESULT STDMETHODCALLTYPE get_SystemProductName(HSTRING* out) override{return Text(L"ForgeStandalone",out);}
 HRESULT STDMETHODCALLTYPE get_SystemSku(HSTRING* out) override{return Text(L"Desktop",out);}
};
class DeviceInfoFactory final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IActivationFactory>{
public:HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override{return Make<DeviceInfo>().CopyTo(out);}
};
HRESULT DeviceInfoActivation(REFIID iid,void** out){return Make<DeviceInfoFactory>().CopyTo(iid,out);}
