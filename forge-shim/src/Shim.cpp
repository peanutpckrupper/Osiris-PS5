#include "NativeDesktopHost.h"
#include <roapi.h>
#include <wrl.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>
using namespace Microsoft::WRL;
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::Foundation;
static decltype(&RoGetActivationFactory) realFactory = RoGetActivationFactory;
static decltype(&RoActivateInstance) realActivate = RoActivateInstance;
void* DxgiReplacement(const char*);
void* WineApiReplacement(const char*);
static void Log(const wchar_t* kind,HSTRING name,REFIID iid,HRESULT hr) {
 wchar_t guid[64],buf[1024];StringFromGUID2(iid,guid,64);
 swprintf_s(buf,L"[ForgeShim] %s %s iid=%s hr=%08X tid=%lu\n",kind,WindowsGetStringRawBuffer(name,nullptr),guid,hr,GetCurrentThreadId());OutputDebugStringW(buf);
}
class Application final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IActivationFactory,ICoreApplication,ICoreImmersiveApplication> {
 long long next=0;
public:
 HRESULT STDMETHODCALLTYPE get_MainView(ICoreApplicationView** out) override{OutputDebugStringA("[ForgeShim] CoreApplication.MainView\n");return h5::desktop::GetCurrentView(out);}
 HRESULT STDMETHODCALLTYPE get_Views(__FIVectorView_1_Windows__CApplicationModel__CCore__CCoreApplicationView** out) override{if(out)*out=nullptr;OutputDebugStringA("[ForgeShim] unsupported CoreApplication.Views\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE CreateNewView(HSTRING,HSTRING,ICoreApplicationView** out) override{if(out)*out=nullptr;return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {using Base=RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IActivationFactory,ICoreApplication,ICoreImmersiveApplication>;HRESULT hr=Base::QueryInterface(iid,out);wchar_t id[64],line[160];StringFromGUID2(iid,id,64);swprintf_s(line,L"[ForgeShim] CoreApplication QI %s hr=%08X\n",id,hr);if(FAILED(hr))OutputDebugStringW(line);return hr;}
 HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override{if(out)*out=nullptr;return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE get_Id(HSTRING* out) override{return WindowsCreateString(L"ForgeStandalone",15,out);}
 HRESULT STDMETHODCALLTYPE add_Suspending(__FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs*,EventRegistrationToken* t) override{if(!t)return E_POINTER;t->value=++next;return S_OK;}
 HRESULT STDMETHODCALLTYPE remove_Suspending(EventRegistrationToken) override{return S_OK;}
 HRESULT STDMETHODCALLTYPE add_Resuming(__FIEventHandler_1_IInspectable*,EventRegistrationToken* t) override{if(!t)return E_POINTER;t->value=++next;return S_OK;}
 HRESULT STDMETHODCALLTYPE remove_Resuming(EventRegistrationToken) override{return S_OK;}
 HRESULT STDMETHODCALLTYPE get_Properties(ABI::Windows::Foundation::Collections::IPropertySet** out) override{if(out)*out=nullptr;OutputDebugStringA("[ForgeShim] unsupported CoreApplication.Properties\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetCurrentView(ICoreApplicationView** out) override{return h5::desktop::GetCurrentView(out);}
 HRESULT STDMETHODCALLTYPE Run(IFrameworkViewSource* source) override{OutputDebugStringA("[ForgeShim] desktop CoreApplication.Run\n");return h5::desktop::Run(source);}
 HRESULT STDMETHODCALLTYPE RunWithActivationFactories(IGetActivationFactory*) override{return E_NOTIMPL;}
};
static ComPtr<Application> application;
HRESULT PackageActivation(REFIID,void**);
HRESULT ApplicationViewActivation(REFIID,void**);
HRESULT ApplicationDataActivation(REFIID,void**);
HRESULT MouseActivation(REFIID,void**);
HRESULT DisplayActivation(REFIID,void**);
HRESULT DeviceInfoActivation(REFIID,void**);
HRESULT WebAuthManagerActivation(REFIID,void**);
HRESULT GamepadActivation(REFIID,void**);
void ApplyOfflinePatches();
void InstallCampaignEngine();
void InstallAutomation();
void InstallLogFile();
void InstallDisplayOptions();
void InstallWrapperTrace();
void InstallPackageIdentity();
void InstallHttpTrace();
void InstallFileTrace();
void InstallGpuTrace();
HRESULT WebTokenRequestActivation(REFIID,void**);
HRESULT GameSaveProviderActivation(REFIID,void**);
static unsigned Patch(HMODULE module);
bool CompatFactory(HSTRING,REFIID,void**,HRESULT(WINAPI*)(HSTRING,REFIID,void**),HRESULT&);
static HRESULT LocalXboxFactory(HSTRING name,REFIID iid,void** out){
 static HMODULE module=[]{HMODULE m=LoadLibraryW(L"Microsoft.Xbox.Services.dll");if(m){char l[80];sprintf_s(l,"[ForgeShim] Xbox Services imports patched=%u\n",Patch(m));OutputDebugStringA(l);}return m;}();
 using GetFactory=HRESULT(WINAPI*)(HSTRING,IActivationFactory**);
 auto get=module?reinterpret_cast<GetFactory>(GetProcAddress(module,"DllGetActivationFactory")):nullptr;
 if(!get)return HRESULT_FROM_WIN32(GetLastError());ComPtr<IActivationFactory> factory;HRESULT hr=get(name,&factory);return SUCCEEDED(hr)?factory.CopyTo(iid,out):hr;
}
static const bool realSignIn=GetEnvironmentVariableW(L"FORGE_REAL_SIGNIN",nullptr,0)!=0;
HRESULT WINAPI Factory(HSTRING name,REFIID iid,void** out) {
 if(!out)return E_POINTER;*out=nullptr;
 const wchar_t* raw=WindowsGetStringRawBuffer(name,nullptr);HRESULT hr;
 if(wcscmp(raw,L"Windows.ApplicationModel.Core.CoreApplication")==0)hr=application.CopyTo(iid,out);
 else if(wcscmp(raw,L"Windows.ApplicationModel.Package")==0)hr=PackageActivation(iid,out);
 else if(wcscmp(raw,L"Windows.UI.ViewManagement.ApplicationView")==0)hr=ApplicationViewActivation(iid,out);
 else if(wcscmp(raw,L"Windows.Storage.ApplicationData")==0)hr=ApplicationDataActivation(iid,out);
 else if(wcscmp(raw,L"Windows.Devices.Input.MouseDevice")==0)hr=MouseActivation(iid,out);
 else if(wcscmp(raw,L"Windows.Graphics.Display.DisplayInformation")==0)hr=DisplayActivation(iid,out);
 else if(wcscmp(raw,L"Windows.Security.ExchangeActiveSyncProvisioning.EasClientDeviceInformation")==0)hr=DeviceInfoActivation(iid,out);
 else if(realSignIn && wcsncmp(raw,L"Windows.Security.Authentication.Web.Core.",41)==0)hr=realFactory(name,iid,out);   // FORGE_REAL_SIGNIN: use Windows WAM
 else if(wcscmp(raw,L"Windows.Security.Authentication.Web.Core.WebAuthenticationCoreManager")==0)hr=WebAuthManagerActivation(iid,out);
 else if(wcscmp(raw,L"Windows.Security.Authentication.Web.Core.WebTokenRequest")==0)hr=WebTokenRequestActivation(iid,out);
 else if(wcscmp(raw,L"Windows.Gaming.Input.Gamepad")==0)hr=GamepadActivation(iid,out);
 else if(wcscmp(raw,L"Windows.Gaming.XboxLive.Storage.GameSaveProvider")==0 && !GetEnvironmentVariableW(L"FORGE_REAL_GAMESAVE",nullptr,0))hr=GameSaveProviderActivation(iid,out);
 else if(wcsncmp(raw,L"Microsoft.Xbox.Services.",24)==0)hr=LocalXboxFactory(name,iid,out);
 else if(wcscmp(raw,L"Windows.UI.Core.CoreWindow")==0)hr=h5::desktop::GetCoreWindowStaticFactory(iid,out);
 else if(CompatFactory(name,iid,out,realFactory,hr)){}
 else hr=realFactory(name,iid,out);
 Log(L"Factory",name,iid,hr);return hr;
}
HRESULT WINAPI Activate(HSTRING name,IInspectable** out){Log(L"Activate",name,__uuidof(IInspectable),S_OK);return realActivate(name,out);}
// Offline privileges: every gaming privilege check (multiplayer, UGC, ...) is granted.
static HRESULT WINAPI PrivSilently(UINT32 p, HSTRING, HSTRING, BOOL* has) { if (has) *has = TRUE; char l[96]; sprintf_s(l, "[ForgeShim] privilege %u granted (silent)\n", p); OutputDebugStringA(l); return S_OK; }
static HRESULT WINAPI PrivSilentlyForUser(IInspectable*, UINT32 p, HSTRING a, HSTRING b, BOOL* has) { return PrivSilently(p, a, b, has); }
using GameUiDone = void(CALLBACK*)(HRESULT, void*);
static HRESULT WINAPI PrivWithUI(UINT32 p, HSTRING, HSTRING, HSTRING, GameUiDone done, void* ctx) { char l[96]; sprintf_s(l, "[ForgeShim] privilege %u granted (UI)\n", p); OutputDebugStringA(l); if (done) done(S_OK, ctx); return S_OK; }
static HRESULT WINAPI PrivWithUIForUser(IInspectable*, UINT32 p, HSTRING a, HSTRING b, HSTRING c, GameUiDone done, void* ctx) { return PrivWithUI(p, a, b, c, done, ctx); }
static void* PrivilegeReplacement(const char* name) {
 if(strcmp(name,"CheckGamingPrivilegeSilently")==0)return reinterpret_cast<void*>(PrivSilently);
 if(strcmp(name,"CheckGamingPrivilegeSilentlyForUser")==0)return reinterpret_cast<void*>(PrivSilentlyForUser);
 if(strcmp(name,"CheckGamingPrivilegeWithUI")==0)return reinterpret_cast<void*>(PrivWithUI);
 if(strcmp(name,"CheckGamingPrivilegeWithUIForUser")==0)return reinterpret_cast<void*>(PrivWithUIForUser);
 return nullptr;
}
static unsigned Patch(HMODULE module) {
 auto base=reinterpret_cast<unsigned char*>(module);auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
 if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return 0;auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
 auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!dir.VirtualAddress)return 0;
 unsigned count=0;
 for(auto desc=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+dir.VirtualAddress);desc->Name;++desc){
  if(!desc->OriginalFirstThunk)continue;
  auto names=reinterpret_cast<IMAGE_THUNK_DATA64*>(base+desc->OriginalFirstThunk);auto slots=reinterpret_cast<IMAGE_THUNK_DATA64*>(base+desc->FirstThunk);
  for(;names->u1.AddressOfData;++names,++slots){if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
   auto entry=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);void* replacement=DxgiReplacement(reinterpret_cast<char*>(entry->Name));if(!replacement)replacement=PrivilegeReplacement(reinterpret_cast<char*>(entry->Name));if(!replacement)replacement=WineApiReplacement(reinterpret_cast<char*>(entry->Name));
   if(strcmp(reinterpret_cast<char*>(entry->Name),"RoGetActivationFactory")==0)replacement=reinterpret_cast<void*>(Factory);
   if(strcmp(reinterpret_cast<char*>(entry->Name),"RoActivateInstance")==0)replacement=reinterpret_cast<void*>(Activate);
   if(!replacement)continue;DWORD old;if(!VirtualProtect(&slots->u1.Function,8,PAGE_READWRITE,&old))continue;
   InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&slots->u1.Function),replacement);DWORD ignored;VirtualProtect(&slots->u1.Function,8,old,&ignored);++count;
  }
 }return count;
}
extern "C" __declspec(dllexport) DWORD WINAPI InitializeShim(void*) {
 InstallLogFile();
 application=Make<Application>();if(!application)return static_cast<DWORD>(E_OUTOFMEMORY);
 HMODULE self=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&InitializeShim),&self);
 auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());MODULEENTRY32W entry{sizeof(entry)};unsigned patched=0;
 if(Module32FirstW(snapshot,&entry))do{if(entry.hModule!=self)patched+=Patch(entry.hModule);}while(Module32NextW(snapshot,&entry));CloseHandle(snapshot);
 ApplyOfflinePatches();
 InstallCampaignEngine();
 InstallDisplayOptions();
 InstallGpuTrace();
 InstallAutomation();
 InstallWrapperTrace();
 InstallPackageIdentity();InstallHttpTrace();InstallFileTrace();
 char line[100];sprintf_s(line,"[ForgeShim] initialized patched_slots=%u\n",patched);OutputDebugStringA(line);return patched?0:ERROR_NOT_FOUND;
}
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){return TRUE;}


