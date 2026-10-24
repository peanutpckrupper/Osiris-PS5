#include <windows.h>
#include <windows.storage.h>
#include <wrl.h>
#include <filesystem>
#include <string>
#include <roapi.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Storage;
using namespace ABI::Windows::Foundation;
class LocalFolder final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IStorageFolder,IStorageItem>{
 std::wstring path;
public:
 explicit LocalFolder(std::wstring p):path(std::move(p)){}
 HRESULT STDMETHODCALLTYPE CreateFileAsyncOverloadDefaultOptions(
                    HSTRING desiredName,
                    __FIAsyncOperation_1_Windows__CStorage__CStorageFile** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageFolder.CreateFileAsyncOverloadDefaultOptions\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE CreateFileAsync(
                    HSTRING desiredName,
                    ABI::Windows::Storage::CreationCollisionOption options,
                    __FIAsyncOperation_1_Windows__CStorage__CStorageFile** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageFolder.CreateFileAsync\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE CreateFolderAsyncOverloadDefaultOptions(
                    HSTRING desiredName,
                    __FIAsyncOperation_1_Windows__CStorage__CStorageFolder** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageFolder.CreateFolderAsyncOverloadDefaultOptions\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE CreateFolderAsync(
                    HSTRING desiredName,
                    ABI::Windows::Storage::CreationCollisionOption options,
                    __FIAsyncOperation_1_Windows__CStorage__CStorageFolder** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageFolder.CreateFolderAsync\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetFileAsync(
                    HSTRING name,
                    __FIAsyncOperation_1_Windows__CStorage__CStorageFile** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageFolder.GetFileAsync\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetFolderAsync(
                    HSTRING name,
                    __FIAsyncOperation_1_Windows__CStorage__CStorageFolder** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageFolder.GetFolderAsync\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetItemAsync(
                    HSTRING name,
                    __FIAsyncOperation_1_Windows__CStorage__CIStorageItem** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageFolder.GetItemAsync\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetFilesAsyncOverloadDefaultOptionsStartAndCount(
                    __FIAsyncOperation_1___FIVectorView_1_Windows__CStorage__CStorageFile** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageFolder.GetFilesAsyncOverloadDefaultOptionsStartAndCount\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetFoldersAsyncOverloadDefaultOptionsStartAndCount(
                    __FIAsyncOperation_1___FIVectorView_1_Windows__CStorage__CStorageFolder** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageFolder.GetFoldersAsyncOverloadDefaultOptionsStartAndCount\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetItemsAsyncOverloadDefaultStartAndCount(
                    __FIAsyncOperation_1___FIVectorView_1_Windows__CStorage__CIStorageItem** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageFolder.GetItemsAsyncOverloadDefaultStartAndCount\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE RenameAsyncOverloadDefaultOptions(
                    HSTRING desiredName,
                    ABI::Windows::Foundation::IAsyncAction** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageItem.RenameAsyncOverloadDefaultOptions\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE RenameAsync(
                    HSTRING desiredName,
                    ABI::Windows::Storage::NameCollisionOption option,
                    ABI::Windows::Foundation::IAsyncAction** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageItem.RenameAsync\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE DeleteAsyncOverloadDefaultOptions(
                    ABI::Windows::Foundation::IAsyncAction** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageItem.DeleteAsyncOverloadDefaultOptions\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE DeleteAsync(
                    ABI::Windows::Storage::StorageDeleteOption option,
                    ABI::Windows::Foundation::IAsyncAction** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageItem.DeleteAsync\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetBasicPropertiesAsync(
                    __FIAsyncOperation_1_Windows__CStorage__CFileProperties__CBasicProperties** operation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageItem.GetBasicPropertiesAsync\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE get_Name(
                    HSTRING* value
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageItem.get_Name\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE get_Path(
                    HSTRING* value
                    ) override { OutputDebugStringA("[ForgeShim] StorageItem.Path\n");return WindowsCreateString(path.c_str(),static_cast<UINT32>(path.size()),value);}
 HRESULT STDMETHODCALLTYPE get_Attributes(
                    ABI::Windows::Storage::FileAttributes* value
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageItem.get_Attributes\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE get_DateCreated(
                    ABI::Windows::Foundation::DateTime* value
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageItem.get_DateCreated\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE IsOfType(
                    ABI::Windows::Storage::StorageItemTypes type,
                    boolean* value
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IStorageItem.IsOfType\n");return E_NOTIMPL;}
};
HRESULT CompatPropertySet(ABI::Windows::Foundation::Collections::IPropertySet**);
class LocalSettings final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IApplicationDataContainer>{
 ComPtr<ABI::Windows::Foundation::Collections::IPropertySet> values;
public:
 LocalSettings(){ComPtr<IInspectable> object;HRESULT hr=RoActivateInstance(Wrappers::HStringReference(L"Windows.Foundation.Collections.PropertySet").Get(),&object);if(SUCCEEDED(hr))object.As(&values);if(!values)CompatPropertySet(&values);}
 HRESULT STDMETHODCALLTYPE get_Name(HSTRING* out) override{return WindowsCreateString(L"LocalSettings",13,out);}
 HRESULT STDMETHODCALLTYPE get_Locality(ApplicationDataLocality* out) override{if(!out)return E_POINTER;*out=ApplicationDataLocality_Local;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_Values(ABI::Windows::Foundation::Collections::IPropertySet** out) override{OutputDebugStringA("[ForgeShim] LocalSettings.Values\n");return values?values.CopyTo(out):E_FAIL;}
 HRESULT STDMETHODCALLTYPE get_Containers(__FIMapView_2_HSTRING_Windows__CStorage__CApplicationDataContainer** out) override{if(out)*out=nullptr;OutputDebugStringA("[ForgeShim] unsupported settings containers\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE CreateContainer(HSTRING,ApplicationDataCreateDisposition,IApplicationDataContainer** out) override{if(out)*out=nullptr;return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE DeleteContainer(HSTRING) override{return E_NOTIMPL;}
};
class LocalApplicationData final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IApplicationData>{
 ComPtr<LocalSettings> settings=Make<LocalSettings>();
 HRESULT Folder(const wchar_t* name,IStorageFolder** out){wchar_t root[32768];if(!GetEnvironmentVariableW(L"LOCALAPPDATA",root,32768))return E_FAIL;auto path=std::filesystem::path(root)/L"ForgeStandalone"/name;std::error_code ec;std::filesystem::create_directories(path,ec);if(ec)return HRESULT_FROM_WIN32(ec.value());return Make<LocalFolder>(path.wstring()).CopyTo(out);}
public:
 HRESULT STDMETHODCALLTYPE get_Version(
                    UINT32* value
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IApplicationData.get_Version\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE SetVersionAsync(
                    UINT32 desiredVersion,
                    ABI::Windows::Storage::IApplicationDataSetVersionHandler* handler,
                    ABI::Windows::Foundation::IAsyncAction** setVersionOperation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IApplicationData.SetVersionAsync\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE ClearAllAsync(
                    ABI::Windows::Foundation::IAsyncAction** clearOperation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IApplicationData.ClearAllAsync\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE ClearAsync(
                    ABI::Windows::Storage::ApplicationDataLocality locality,
                    ABI::Windows::Foundation::IAsyncAction** clearOperation
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IApplicationData.ClearAsync\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE get_LocalSettings(
                    ABI::Windows::Storage::IApplicationDataContainer** value
                    ) override { return settings.CopyTo(value);}
 HRESULT STDMETHODCALLTYPE get_RoamingSettings(
                    ABI::Windows::Storage::IApplicationDataContainer** value
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IApplicationData.get_RoamingSettings\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE get_LocalFolder(
                    ABI::Windows::Storage::IStorageFolder** value
                    ) override { return Folder(L"LocalState",value);}
 HRESULT STDMETHODCALLTYPE get_RoamingFolder(
                    ABI::Windows::Storage::IStorageFolder** value
                    ) override { return Folder(L"RoamingState",value);}
 HRESULT STDMETHODCALLTYPE get_TemporaryFolder(
                    ABI::Windows::Storage::IStorageFolder** value
                    ) override { return Folder(L"TempState",value);}
 HRESULT STDMETHODCALLTYPE add_DataChanged(
                    __FITypedEventHandler_2_Windows__CStorage__CApplicationData_IInspectable* handler,
                    EventRegistrationToken* token
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IApplicationData.add_DataChanged\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE remove_DataChanged(
                    EventRegistrationToken token
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IApplicationData.remove_DataChanged\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE SignalDataChanged(void) override { OutputDebugStringA("[ForgeShim] unsupported IApplicationData.SignalDataChanged\n");return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE get_RoamingStorageQuota(
                    UINT64* value
                    ) override { OutputDebugStringA("[ForgeShim] unsupported IApplicationData.get_RoamingStorageQuota\n");return E_NOTIMPL;}
};
class LocalDataFactory final:public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>,IActivationFactory,IApplicationDataStatics>{
 ComPtr<LocalApplicationData> data=Make<LocalApplicationData>();
public:
 HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override{if(out)*out=nullptr;return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE get_Current(IApplicationData** out) override{OutputDebugStringA("[ForgeShim] ApplicationData.Current (local)\n");return data.CopyTo(out);}
};
HRESULT ApplicationDataActivation(REFIID iid,void** out){return Make<LocalDataFactory>().CopyTo(iid,out);}
HRESULT StorageFolderForPath(const wchar_t* path,IStorageFolder** out){return Make<LocalFolder>(std::wstring(path)).CopyTo(out);}


