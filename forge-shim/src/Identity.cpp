#include <windows.h>
#include <windows.applicationmodel.h>
#include <windows.storage.h>
#include <wrl.h>
#include <winstring.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::ApplicationModel;
static HRESULT String(const wchar_t* s,HSTRING* out){return WindowsCreateString(s,static_cast<UINT32>(wcslen(s)),out);}
HRESULT StorageFolderForPath(const wchar_t*,ABI::Windows::Storage::IStorageFolder**);
class SyntheticPackageId final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IPackageId>{
public:
 HRESULT STDMETHODCALLTYPE get_Name(HSTRING* out) override{return String(L"ForgeStandalone",out);}
 HRESULT STDMETHODCALLTYPE get_Version(PackageVersion* out) override{if(!out)return E_POINTER;*out={1,194,6192,2};return S_OK;}
 HRESULT STDMETHODCALLTYPE get_Architecture(ABI::Windows::System::ProcessorArchitecture* out) override{if(!out)return E_POINTER;*out=static_cast<ABI::Windows::System::ProcessorArchitecture>(9);return S_OK;}
 HRESULT STDMETHODCALLTYPE get_ResourceId(HSTRING* out) override{return String(L"",out);}
 HRESULT STDMETHODCALLTYPE get_Publisher(HSTRING* out) override{return String(L"CN=ForgeStandalone",out);}
 HRESULT STDMETHODCALLTYPE get_PublisherId(HSTRING* out) override{return String(L"Standalone",out);}
 HRESULT STDMETHODCALLTYPE get_FullName(HSTRING* out) override{return String(L"ForgeStandalone_1.194.6192.2_x64__Standalone",out);}
 HRESULT STDMETHODCALLTYPE get_FamilyName(HSTRING* out) override{return String(L"ForgeStandalone_Standalone",out);}
};
class SyntheticPackage final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IPackage>{
public:
 HRESULT STDMETHODCALLTYPE get_Id(IPackageId** out) override{OutputDebugStringA("[ForgeShim] Package.Id\n");return Make<SyntheticPackageId>().CopyTo(out);}
 HRESULT STDMETHODCALLTYPE get_InstalledLocation(ABI::Windows::Storage::IStorageFolder** out) override{wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);if(!n||n==32768)return E_FAIL;auto slash=wcsrchr(path,L'\\');if(!slash)return E_FAIL;*slash=0;OutputDebugStringA("[ForgeShim] Package.InstalledLocation (dev root)\n");return StorageFolderForPath(path,out);}
 HRESULT STDMETHODCALLTYPE get_IsFramework(boolean* out) override{if(!out)return E_POINTER;*out=false;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_Dependencies(ABI::Windows::Foundation::Collections::IVectorView<ABI::Windows::ApplicationModel::Package*>** out) override{if(out)*out=nullptr;OutputDebugStringA("[ForgeShim] unsupported Package.Dependencies\n");return E_NOTIMPL;}
};
class PackageFactory final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IActivationFactory,IPackageStatics>{
 ComPtr<SyntheticPackage> package=Make<SyntheticPackage>();
public:
 HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override{if(out)*out=nullptr;return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE get_Current(IPackage** out) override{OutputDebugStringA("[ForgeShim] Package.Current (synthetic)\n");return package.CopyTo(out);}
};
HRESULT PackageActivation(REFIID iid,void** out){return Make<PackageFactory>().CopyTo(iid,out);}

