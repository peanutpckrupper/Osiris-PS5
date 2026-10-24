namespace S = ABI::Windows::Services::Store;
class StoreLicenseInfo final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, S::IStoreAppLicense> {
    InspectableClass(L"Windows.Services.Store.StoreAppLicense", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE get_SkuStoreId(HSTRING * value) override { return Str(L"", value); }
    HRESULT STDMETHODCALLTYPE get_IsActive(boolean * value) override { *value = true; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsTrial(boolean * value) override { *value = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ExpirationDate(ABI::Windows::Foundation::DateTime * value) override { value->UniversalTime = 0x7fffffffffffffffLL; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ExtendedJsonData(HSTRING * value) override { return Str(L"{}", value); }
    HRESULT STDMETHODCALLTYPE get_AddOnLicenses(__FIMapView_2_HSTRING_Windows__CServices__CStore__CStoreLicense * * value) override { CompatMiss("IStoreAppLicense.get_AddOnLicenses (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_TrialTimeRemaining(ABI::Windows::Foundation::TimeSpan * value) override { value->Duration = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsTrialOwnedByThisUser(boolean * value) override { *value = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_TrialUniqueId(HSTRING * value) override { return Str(L"", value); }
};
class StoreCtx final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, S::IStoreContext> {
    InspectableClass(L"Windows.Services.Store.StoreContext", BaseTrust)
public:
    INT64 next_ = 1;
    HRESULT STDMETHODCALLTYPE get_User(ABI::Windows::System::IUser * * value) override { *value = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE add_OfflineLicensesChanged(__FITypedEventHandler_2_Windows__CServices__CStore__CStoreContext_IInspectable * handler, EventRegistrationToken * token) override { token->value = next_++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_OfflineLicensesChanged(EventRegistrationToken token) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCustomerPurchaseIdAsync(HSTRING serviceTicket, HSTRING publisherUserId, __FIAsyncOperation_1_HSTRING * * operation) override { CompatMiss("IStoreContext.GetCustomerPurchaseIdAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCustomerCollectionsIdAsync(HSTRING serviceTicket, HSTRING publisherUserId, __FIAsyncOperation_1_HSTRING * * operation) override { CompatMiss("IStoreContext.GetCustomerCollectionsIdAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetAppLicenseAsync(__FIAsyncOperation_1_Windows__CServices__CStore__CStoreAppLicense * * operation) override { CompatNote("StoreContext.GetAppLicenseAsync (offline: full licence)"); return Make<Done<S::StoreAppLicense*, S::IStoreAppLicense>>(Make<StoreLicenseInfo>().Get()).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE GetStoreProductForCurrentAppAsync(__FIAsyncOperation_1_Windows__CServices__CStore__CStoreProductResult * * operation) override { CompatMiss("IStoreContext.GetStoreProductForCurrentAppAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetStoreProductsAsync(__FIIterable_1_HSTRING * productKinds, __FIIterable_1_HSTRING * storeIds, __FIAsyncOperation_1_Windows__CServices__CStore__CStoreProductQueryResult * * operation) override { CompatMiss("IStoreContext.GetStoreProductsAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetAssociatedStoreProductsAsync(__FIIterable_1_HSTRING * productKinds, __FIAsyncOperation_1_Windows__CServices__CStore__CStoreProductQueryResult * * operation) override { CompatMiss("IStoreContext.GetAssociatedStoreProductsAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetAssociatedStoreProductsWithPagingAsync(__FIIterable_1_HSTRING * productKinds, UINT32 maxItemsToRetrievePerPage, __FIAsyncOperation_1_Windows__CServices__CStore__CStoreProductPagedQueryResult * * operation) override { CompatMiss("IStoreContext.GetAssociatedStoreProductsWithPagingAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetUserCollectionAsync(__FIIterable_1_HSTRING * productKinds, __FIAsyncOperation_1_Windows__CServices__CStore__CStoreProductQueryResult * * operation) override { CompatMiss("IStoreContext.GetUserCollectionAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetUserCollectionWithPagingAsync(__FIIterable_1_HSTRING * productKinds, UINT32 maxItemsToRetrievePerPage, __FIAsyncOperation_1_Windows__CServices__CStore__CStoreProductPagedQueryResult * * operation) override { CompatMiss("IStoreContext.GetUserCollectionWithPagingAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ReportConsumableFulfillmentAsync(HSTRING productStoreId, UINT32 quantity, GUID trackingId, __FIAsyncOperation_1_Windows__CServices__CStore__CStoreConsumableResult * * operation) override { CompatMiss("IStoreContext.ReportConsumableFulfillmentAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetConsumableBalanceRemainingAsync(HSTRING productStoreId, __FIAsyncOperation_1_Windows__CServices__CStore__CStoreConsumableResult * * operation) override { CompatMiss("IStoreContext.GetConsumableBalanceRemainingAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE AcquireStoreLicenseForOptionalPackageAsync(ABI::Windows::ApplicationModel::IPackage * optionalPackage, __FIAsyncOperation_1_Windows__CServices__CStore__CStoreAcquireLicenseResult * * operation) override { CompatMiss("IStoreContext.AcquireStoreLicenseForOptionalPackageAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RequestPurchaseAsync(HSTRING storeId, __FIAsyncOperation_1_Windows__CServices__CStore__CStorePurchaseResult * * operation) override { CompatMiss("IStoreContext.RequestPurchaseAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RequestPurchaseWithPurchasePropertiesAsync(HSTRING storeId, ABI::Windows::Services::Store::IStorePurchaseProperties * storePurchaseProperties, __FIAsyncOperation_1_Windows__CServices__CStore__CStorePurchaseResult * * operation) override { CompatMiss("IStoreContext.RequestPurchaseWithPurchasePropertiesAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetAppAndOptionalStorePackageUpdatesAsync(__FIAsyncOperation_1___FIVectorView_1_Windows__CServices__CStore__CStorePackageUpdate * * operation) override { return Make<Done<ABI::Windows::Foundation::Collections::IVectorView<S::StorePackageUpdate*>*, ABI::Windows::Foundation::Collections::IVectorView<S::StorePackageUpdate*>>>(Make<EmptyView<S::StorePackageUpdate*>>().Get()).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE RequestDownloadStorePackageUpdatesAsync(__FIIterable_1_Windows__CServices__CStore__CStorePackageUpdate * storePackageUpdates, __FIAsyncOperationWithProgress_2_Windows__CServices__CStore__CStorePackageUpdateResult_Windows__CServices__CStore__CStorePackageUpdateStatus * * operation) override { CompatMiss("IStoreContext.RequestDownloadStorePackageUpdatesAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RequestDownloadAndInstallStorePackageUpdatesAsync(__FIIterable_1_Windows__CServices__CStore__CStorePackageUpdate * storePackageUpdates, __FIAsyncOperationWithProgress_2_Windows__CServices__CStore__CStorePackageUpdateResult_Windows__CServices__CStore__CStorePackageUpdateStatus * * operation) override { CompatMiss("IStoreContext.RequestDownloadAndInstallStorePackageUpdatesAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RequestDownloadAndInstallStorePackagesAsync(__FIIterable_1_HSTRING * storeIds, __FIAsyncOperationWithProgress_2_Windows__CServices__CStore__CStorePackageUpdateResult_Windows__CServices__CStore__CStorePackageUpdateStatus * * operation) override { CompatMiss("IStoreContext.RequestDownloadAndInstallStorePackagesAsync"); return E_NOTIMPL; }
};
class StoreCtxStatics final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, S::IStoreContextStatics> {
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { if (out) *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDefault(ABI::Windows::Services::Store::IStoreContext * * value) override { return Make<StoreCtx>().CopyTo(value); }
    HRESULT STDMETHODCALLTYPE GetForUser(ABI::Windows::System::IUser * user, ABI::Windows::Services::Store::IStoreContext * * value) override { return Make<StoreCtx>().CopyTo(value); }
};
class LauncherStatics final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, ABI::Windows::System::ILauncherStatics, ABI::Windows::System::ILauncherStatics2, ABI::Windows::System::ILauncherStatics3, ABI::Windows::System::ILauncherStatics4> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { HRESULT h = RuntimeClass::QueryInterface(riid, ppv); if (FAILED(h)) CompatMissQI("LauncherStatics", riid); return h; }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { if (out) *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LaunchFileAsync(ABI::Windows::Storage::IStorageFile * file, __FIAsyncOperation_1_boolean * * operation) override { CompatNote("Launcher: nothing is launched offline"); return Make<DoneValue<bool>>(false).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE LaunchFileWithOptionsAsync(ABI::Windows::Storage::IStorageFile * file, ABI::Windows::System::ILauncherOptions * options, __FIAsyncOperation_1_boolean * * operation) override { CompatNote("Launcher: nothing is launched offline"); return Make<DoneValue<bool>>(false).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE LaunchUriAsync(ABI::Windows::Foundation::IUriRuntimeClass * uri, __FIAsyncOperation_1_boolean * * operation) override { LogUri("Launcher.LaunchUriAsync", uri); return Make<DoneValue<bool>>(false).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE LaunchUriWithOptionsAsync(ABI::Windows::Foundation::IUriRuntimeClass * uri, ABI::Windows::System::ILauncherOptions * options, __FIAsyncOperation_1_boolean * * operation) override { LogUri("Launcher.LaunchUriWithOptionsAsync", uri); return Make<DoneValue<bool>>(false).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE LaunchUriForResultsAsync(ABI::Windows::Foundation::IUriRuntimeClass * uri, ABI::Windows::System::ILauncherOptions * options, __FIAsyncOperation_1_Windows__CSystem__CLaunchUriResult * * operation) override { CompatMiss("ILauncherStatics2.LaunchUriForResultsAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LaunchUriForResultsWithDataAsync(ABI::Windows::Foundation::IUriRuntimeClass * uri, ABI::Windows::System::ILauncherOptions * options, ABI::Windows::Foundation::Collections::IPropertySet * inputData, __FIAsyncOperation_1_Windows__CSystem__CLaunchUriResult * * operation) override { CompatMiss("ILauncherStatics2.LaunchUriForResultsWithDataAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LaunchUriWithDataAsync(ABI::Windows::Foundation::IUriRuntimeClass * uri, ABI::Windows::System::ILauncherOptions * options, ABI::Windows::Foundation::Collections::IPropertySet * inputData, __FIAsyncOperation_1_boolean * * operation) override { CompatMiss("ILauncherStatics2.LaunchUriWithDataAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryUriSupportAsync(ABI::Windows::Foundation::IUriRuntimeClass * uri, ABI::Windows::System::LaunchQuerySupportType launchQuerySupportType, __FIAsyncOperation_1_Windows__CSystem__CLaunchQuerySupportStatus * * operation) override { LogUri("Launcher.QueryUriSupportAsync", uri); return Make<DoneValue<ABI::Windows::System::LaunchQuerySupportStatus>>(ABI::Windows::System::LaunchQuerySupportStatus_AppNotInstalled).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE QueryUriSupportWithPackageFamilyNameAsync(ABI::Windows::Foundation::IUriRuntimeClass * uri, ABI::Windows::System::LaunchQuerySupportType launchQuerySupportType, HSTRING packageFamilyName, __FIAsyncOperation_1_Windows__CSystem__CLaunchQuerySupportStatus * * operation) override { LogUri("Launcher.QueryUriSupportWithPackageFamilyNameAsync", uri); LogText("  package", packageFamilyName); return Make<DoneValue<ABI::Windows::System::LaunchQuerySupportStatus>>(ABI::Windows::System::LaunchQuerySupportStatus_AppNotInstalled).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE QueryFileSupportAsync(ABI::Windows::Storage::IStorageFile * file, __FIAsyncOperation_1_Windows__CSystem__CLaunchQuerySupportStatus * * operation) override { CompatMiss("ILauncherStatics2.QueryFileSupportAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryFileSupportWithPackageFamilyNameAsync(ABI::Windows::Storage::IStorageFile * file, HSTRING packageFamilyName, __FIAsyncOperation_1_Windows__CSystem__CLaunchQuerySupportStatus * * operation) override { CompatMiss("ILauncherStatics2.QueryFileSupportWithPackageFamilyNameAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindUriSchemeHandlersAsync(HSTRING scheme, __FIAsyncOperation_1___FIVectorView_1_Windows__CApplicationModel__CAppInfo * * operation) override { CompatMiss("ILauncherStatics2.FindUriSchemeHandlersAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindUriSchemeHandlersWithLaunchUriTypeAsync(HSTRING scheme, ABI::Windows::System::LaunchQuerySupportType launchQuerySupportType, __FIAsyncOperation_1___FIVectorView_1_Windows__CApplicationModel__CAppInfo * * operation) override { CompatMiss("ILauncherStatics2.FindUriSchemeHandlersWithLaunchUriTypeAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindFileHandlersAsync(HSTRING extension, __FIAsyncOperation_1___FIVectorView_1_Windows__CApplicationModel__CAppInfo * * operation) override { CompatMiss("ILauncherStatics2.FindFileHandlersAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LaunchFolderAsync(ABI::Windows::Storage::IStorageFolder * folder, __FIAsyncOperation_1_boolean * * operation) override { CompatMiss("ILauncherStatics3.LaunchFolderAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LaunchFolderWithOptionsAsync(ABI::Windows::Storage::IStorageFolder * folder, ABI::Windows::System::IFolderLauncherOptions * options, __FIAsyncOperation_1_boolean * * operation) override { CompatMiss("ILauncherStatics3.LaunchFolderWithOptionsAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryAppUriSupportAsync(ABI::Windows::Foundation::IUriRuntimeClass * uri, __FIAsyncOperation_1_Windows__CSystem__CLaunchQuerySupportStatus * * operation) override { LogUri("Launcher.QueryAppUriSupportAsync", uri); return Make<DoneValue<ABI::Windows::System::LaunchQuerySupportStatus>>(ABI::Windows::System::LaunchQuerySupportStatus_AppNotInstalled).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE QueryAppUriSupportWithPackageFamilyNameAsync(ABI::Windows::Foundation::IUriRuntimeClass * uri, HSTRING packageFamilyName, __FIAsyncOperation_1_Windows__CSystem__CLaunchQuerySupportStatus * * operation) override { CompatMiss("ILauncherStatics4.QueryAppUriSupportWithPackageFamilyNameAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindAppUriHandlersAsync(ABI::Windows::Foundation::IUriRuntimeClass * uri, __FIAsyncOperation_1___FIVectorView_1_Windows__CApplicationModel__CAppInfo * * operation) override { CompatMiss("ILauncherStatics4.FindAppUriHandlersAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LaunchUriForUserAsync(ABI::Windows::System::IUser * user, ABI::Windows::Foundation::IUriRuntimeClass * uri, __FIAsyncOperation_1_Windows__CSystem__CLaunchUriStatus * * operation) override { CompatMiss("ILauncherStatics4.LaunchUriForUserAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LaunchUriWithOptionsForUserAsync(ABI::Windows::System::IUser * user, ABI::Windows::Foundation::IUriRuntimeClass * uri, ABI::Windows::System::ILauncherOptions * options, __FIAsyncOperation_1_Windows__CSystem__CLaunchUriStatus * * operation) override { CompatMiss("ILauncherStatics4.LaunchUriWithOptionsForUserAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LaunchUriWithDataForUserAsync(ABI::Windows::System::IUser * user, ABI::Windows::Foundation::IUriRuntimeClass * uri, ABI::Windows::System::ILauncherOptions * options, ABI::Windows::Foundation::Collections::IPropertySet * inputData, __FIAsyncOperation_1_Windows__CSystem__CLaunchUriStatus * * operation) override { CompatMiss("ILauncherStatics4.LaunchUriWithDataForUserAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LaunchUriForResultsForUserAsync(ABI::Windows::System::IUser * user, ABI::Windows::Foundation::IUriRuntimeClass * uri, ABI::Windows::System::ILauncherOptions * options, __FIAsyncOperation_1_Windows__CSystem__CLaunchUriResult * * operation) override { CompatMiss("ILauncherStatics4.LaunchUriForResultsForUserAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LaunchUriForResultsWithDataForUserAsync(ABI::Windows::System::IUser * user, ABI::Windows::Foundation::IUriRuntimeClass * uri, ABI::Windows::System::ILauncherOptions * options, ABI::Windows::Foundation::Collections::IPropertySet * inputData, __FIAsyncOperation_1_Windows__CSystem__CLaunchUriResult * * operation) override { CompatMiss("ILauncherStatics4.LaunchUriForResultsWithDataForUserAsync"); return E_NOTIMPL; }
};
class LauncherOptions final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, ABI::Windows::System::ILauncherOptions> {
    InspectableClass(L"Windows.System.LauncherOptions", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE get_TreatAsUntrusted(boolean * value) override { CompatMiss("ILauncherOptions.get_TreatAsUntrusted (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_TreatAsUntrusted(boolean value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_DisplayApplicationPicker(boolean * value) override { CompatMiss("ILauncherOptions.get_DisplayApplicationPicker (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_DisplayApplicationPicker(boolean value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_UI(ABI::Windows::System::ILauncherUIOptions * * value) override { CompatMiss("ILauncherOptions.get_UI (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_PreferredApplicationPackageFamilyName(HSTRING * value) override { CompatMiss("ILauncherOptions.get_PreferredApplicationPackageFamilyName (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_PreferredApplicationPackageFamilyName(HSTRING value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_PreferredApplicationDisplayName(HSTRING * value) override { CompatMiss("ILauncherOptions.get_PreferredApplicationDisplayName (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_PreferredApplicationDisplayName(HSTRING value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_FallbackUri(ABI::Windows::Foundation::IUriRuntimeClass * * value) override { CompatMiss("ILauncherOptions.get_FallbackUri (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_FallbackUri(ABI::Windows::Foundation::IUriRuntimeClass * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ContentType(HSTRING * value) override { CompatMiss("ILauncherOptions.get_ContentType (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_ContentType(HSTRING value) override { return S_OK; }
};
namespace DE = ABI::Windows::Devices::Enumeration;
using DevView = ABI::Windows::Foundation::Collections::IVectorView<DE::DeviceInformation*>;
class DevWatcher final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, DE::IDeviceWatcher> {
    InspectableClass(L"Windows.Devices.Enumeration.DeviceWatcher", BaseTrust)
public:
    ComPtr<ABI::Windows::Foundation::ITypedEventHandler<DE::DeviceWatcher*, IInspectable*>> done_; DE::DeviceWatcherStatus status_ = DE::DeviceWatcherStatus_Created;
    HRESULT STDMETHODCALLTYPE add_Added(__FITypedEventHandler_2_Windows__CDevices__CEnumeration__CDeviceWatcher_Windows__CDevices__CEnumeration__CDeviceInformation * handler, EventRegistrationToken * token) override { static INT64 n = 1; token->value = n++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_Added(EventRegistrationToken token) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_Updated(__FITypedEventHandler_2_Windows__CDevices__CEnumeration__CDeviceWatcher_Windows__CDevices__CEnumeration__CDeviceInformationUpdate * handler, EventRegistrationToken * token) override { static INT64 n = 1; token->value = n++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_Updated(EventRegistrationToken token) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_Removed(__FITypedEventHandler_2_Windows__CDevices__CEnumeration__CDeviceWatcher_Windows__CDevices__CEnumeration__CDeviceInformationUpdate * handler, EventRegistrationToken * token) override { static INT64 n = 1; token->value = n++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_Removed(EventRegistrationToken token) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_EnumerationCompleted(__FITypedEventHandler_2_Windows__CDevices__CEnumeration__CDeviceWatcher_IInspectable * handler, EventRegistrationToken * token) override { done_ = handler; static INT64 n = 1; token->value = n++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_EnumerationCompleted(EventRegistrationToken token) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_Stopped(__FITypedEventHandler_2_Windows__CDevices__CEnumeration__CDeviceWatcher_IInspectable * handler, EventRegistrationToken * token) override { static INT64 n = 1; token->value = n++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_Stopped(EventRegistrationToken token) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Status(ABI::Windows::Devices::Enumeration::DeviceWatcherStatus * status) override { *status = status_; return S_OK; }
    HRESULT STDMETHODCALLTYPE Start(void) override { status_ = DE::DeviceWatcherStatus_EnumerationCompleted; if (done_) done_->Invoke(this, nullptr); return S_OK; }
    HRESULT STDMETHODCALLTYPE Stop(void) override { status_ = DE::DeviceWatcherStatus_Stopped; return S_OK; }
};
class DevInfoStatics final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, DE::IDeviceInformationStatics, DE::IDeviceInformationStatics2> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { HRESULT h = RuntimeClass::QueryInterface(riid, ppv); if (FAILED(h)) CompatMissQI("DevInfoStatics", riid); return h; }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { if (out) *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateFromIdAsync(HSTRING deviceId, __FIAsyncOperation_1_Windows__CDevices__CEnumeration__CDeviceInformation * * asyncOp) override { CompatMiss("IDeviceInformationStatics.CreateFromIdAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateFromIdAsyncAdditionalProperties(HSTRING deviceId, __FIIterable_1_HSTRING * additionalProperties, __FIAsyncOperation_1_Windows__CDevices__CEnumeration__CDeviceInformation * * asyncOp) override { CompatMiss("IDeviceInformationStatics.CreateFromIdAsyncAdditionalProperties"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindAllAsync(__FIAsyncOperation_1_Windows__CDevices__CEnumeration__CDeviceInformationCollection * * asyncOp) override { return Make<Done<DE::DeviceInformationCollection*, DevView>>(Make<EmptyView<DE::DeviceInformation*>>().Get()).CopyTo(asyncOp); }
    HRESULT STDMETHODCALLTYPE FindAllAsyncDeviceClass(ABI::Windows::Devices::Enumeration::DeviceClass deviceClass, __FIAsyncOperation_1_Windows__CDevices__CEnumeration__CDeviceInformationCollection * * asyncOp) override { return Make<Done<DE::DeviceInformationCollection*, DevView>>(Make<EmptyView<DE::DeviceInformation*>>().Get()).CopyTo(asyncOp); }
    HRESULT STDMETHODCALLTYPE FindAllAsyncAqsFilter(HSTRING aqsFilter, __FIAsyncOperation_1_Windows__CDevices__CEnumeration__CDeviceInformationCollection * * asyncOp) override { return Make<Done<DE::DeviceInformationCollection*, DevView>>(Make<EmptyView<DE::DeviceInformation*>>().Get()).CopyTo(asyncOp); }
    HRESULT STDMETHODCALLTYPE FindAllAsyncAqsFilterAndAdditionalProperties(HSTRING aqsFilter, __FIIterable_1_HSTRING * additionalProperties, __FIAsyncOperation_1_Windows__CDevices__CEnumeration__CDeviceInformationCollection * * asyncOp) override { return Make<Done<DE::DeviceInformationCollection*, DevView>>(Make<EmptyView<DE::DeviceInformation*>>().Get()).CopyTo(asyncOp); }
    HRESULT STDMETHODCALLTYPE CreateWatcher(ABI::Windows::Devices::Enumeration::IDeviceWatcher * * watcher) override { return Make<DevWatcher>().CopyTo(watcher); }
    HRESULT STDMETHODCALLTYPE CreateWatcherDeviceClass(ABI::Windows::Devices::Enumeration::DeviceClass deviceClass, ABI::Windows::Devices::Enumeration::IDeviceWatcher * * watcher) override { return Make<DevWatcher>().CopyTo(watcher); }
    HRESULT STDMETHODCALLTYPE CreateWatcherAqsFilter(HSTRING aqsFilter, ABI::Windows::Devices::Enumeration::IDeviceWatcher * * watcher) override { return Make<DevWatcher>().CopyTo(watcher); }
    HRESULT STDMETHODCALLTYPE CreateWatcherAqsFilterAndAdditionalProperties(HSTRING aqsFilter, __FIIterable_1_HSTRING * additionalProperties, ABI::Windows::Devices::Enumeration::IDeviceWatcher * * watcher) override { return Make<DevWatcher>().CopyTo(watcher); }
    HRESULT STDMETHODCALLTYPE GetAqsFilterFromDeviceClass(ABI::Windows::Devices::Enumeration::DeviceClass deviceClass, HSTRING * aqsFilter) override { return Str(L"", aqsFilter); }
    HRESULT STDMETHODCALLTYPE CreateFromIdAsyncWithKindAndAdditionalProperties(HSTRING deviceId, __FIIterable_1_HSTRING * additionalProperties, ABI::Windows::Devices::Enumeration::DeviceInformationKind kind, __FIAsyncOperation_1_Windows__CDevices__CEnumeration__CDeviceInformation * * asyncOp) override { CompatMiss("IDeviceInformationStatics2.CreateFromIdAsyncWithKindAndAdditionalProperties"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindAllAsyncWithKindAqsFilterAndAdditionalProperties(HSTRING aqsFilter, __FIIterable_1_HSTRING * additionalProperties, ABI::Windows::Devices::Enumeration::DeviceInformationKind kind, __FIAsyncOperation_1_Windows__CDevices__CEnumeration__CDeviceInformationCollection * * asyncOp) override { return Make<Done<DE::DeviceInformationCollection*, DevView>>(Make<EmptyView<DE::DeviceInformation*>>().Get()).CopyTo(asyncOp); }
    HRESULT STDMETHODCALLTYPE CreateWatcherWithKindAqsFilterAndAdditionalProperties(HSTRING aqsFilter, __FIIterable_1_HSTRING * additionalProperties, ABI::Windows::Devices::Enumeration::DeviceInformationKind kind, ABI::Windows::Devices::Enumeration::IDeviceWatcher * * watcher) override { return Make<DevWatcher>().CopyTo(watcher); }
};
namespace FD = ABI::Windows::Foundation::Diagnostics;
class LogFields final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, FD::ILoggingFields> {
    InspectableClass(L"Windows.Foundation.Diagnostics.LoggingFields", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE Clear(void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE BeginStruct(HSTRING name) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE BeginStructWithTags(HSTRING name, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EndStruct(void) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddEmpty(HSTRING name) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddEmptyWithFormat(HSTRING name, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddEmptyWithFormatAndTags(HSTRING name, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt8(HSTRING name, BYTE value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt8WithFormat(HSTRING name, BYTE value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt8WithFormatAndTags(HSTRING name, BYTE value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt8Array(HSTRING name, UINT32 __valueSize, BYTE * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt8ArrayWithFormat(HSTRING name, UINT32 __valueSize, BYTE * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt8ArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, BYTE * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt16(HSTRING name, INT16 value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt16WithFormat(HSTRING name, INT16 value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt16WithFormatAndTags(HSTRING name, INT16 value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt16Array(HSTRING name, UINT32 __valueSize, INT16 * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt16ArrayWithFormat(HSTRING name, UINT32 __valueSize, INT16 * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt16ArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, INT16 * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt16(HSTRING name, UINT16 value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt16WithFormat(HSTRING name, UINT16 value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt16WithFormatAndTags(HSTRING name, UINT16 value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt16Array(HSTRING name, UINT32 __valueSize, UINT16 * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt16ArrayWithFormat(HSTRING name, UINT32 __valueSize, UINT16 * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt16ArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, UINT16 * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt32(HSTRING name, INT32 value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt32WithFormat(HSTRING name, INT32 value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt32WithFormatAndTags(HSTRING name, INT32 value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt32Array(HSTRING name, UINT32 __valueSize, INT32 * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt32ArrayWithFormat(HSTRING name, UINT32 __valueSize, INT32 * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt32ArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, INT32 * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt32(HSTRING name, UINT32 value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt32WithFormat(HSTRING name, UINT32 value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt32WithFormatAndTags(HSTRING name, UINT32 value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt32Array(HSTRING name, UINT32 __valueSize, UINT32 * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt32ArrayWithFormat(HSTRING name, UINT32 __valueSize, UINT32 * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt32ArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, UINT32 * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt64(HSTRING name, INT64 value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt64WithFormat(HSTRING name, INT64 value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt64WithFormatAndTags(HSTRING name, INT64 value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt64Array(HSTRING name, UINT32 __valueSize, INT64 * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt64ArrayWithFormat(HSTRING name, UINT32 __valueSize, INT64 * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddInt64ArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, INT64 * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt64(HSTRING name, UINT64 value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt64WithFormat(HSTRING name, UINT64 value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt64WithFormatAndTags(HSTRING name, UINT64 value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt64Array(HSTRING name, UINT32 __valueSize, UINT64 * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt64ArrayWithFormat(HSTRING name, UINT32 __valueSize, UINT64 * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddUInt64ArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, UINT64 * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddSingle(HSTRING name, FLOAT value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddSingleWithFormat(HSTRING name, FLOAT value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddSingleWithFormatAndTags(HSTRING name, FLOAT value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddSingleArray(HSTRING name, UINT32 __valueSize, FLOAT * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddSingleArrayWithFormat(HSTRING name, UINT32 __valueSize, FLOAT * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddSingleArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, FLOAT * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddDouble(HSTRING name, DOUBLE value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddDoubleWithFormat(HSTRING name, DOUBLE value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddDoubleWithFormatAndTags(HSTRING name, DOUBLE value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddDoubleArray(HSTRING name, UINT32 __valueSize, DOUBLE * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddDoubleArrayWithFormat(HSTRING name, UINT32 __valueSize, DOUBLE * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddDoubleArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, DOUBLE * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddChar16(HSTRING name, WCHAR value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddChar16WithFormat(HSTRING name, WCHAR value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddChar16WithFormatAndTags(HSTRING name, WCHAR value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddChar16Array(HSTRING name, UINT32 __valueSize, WCHAR * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddChar16ArrayWithFormat(HSTRING name, UINT32 __valueSize, WCHAR * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddChar16ArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, WCHAR * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddBoolean(HSTRING name, boolean value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddBooleanWithFormat(HSTRING name, boolean value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddBooleanWithFormatAndTags(HSTRING name, boolean value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddBooleanArray(HSTRING name, UINT32 __valueSize, boolean * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddBooleanArrayWithFormat(HSTRING name, UINT32 __valueSize, boolean * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddBooleanArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, boolean * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddString(HSTRING name, HSTRING value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddStringWithFormat(HSTRING name, HSTRING value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddStringWithFormatAndTags(HSTRING name, HSTRING value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddStringArray(HSTRING name, UINT32 __valueSize, HSTRING * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddStringArrayWithFormat(HSTRING name, UINT32 __valueSize, HSTRING * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddStringArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, HSTRING * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddGuid(HSTRING name, GUID value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddGuidWithFormat(HSTRING name, GUID value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddGuidWithFormatAndTags(HSTRING name, GUID value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddGuidArray(HSTRING name, UINT32 __valueSize, GUID * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddGuidArrayWithFormat(HSTRING name, UINT32 __valueSize, GUID * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddGuidArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, GUID * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddDateTime(HSTRING name, ABI::Windows::Foundation::DateTime value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddDateTimeWithFormat(HSTRING name, ABI::Windows::Foundation::DateTime value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddDateTimeWithFormatAndTags(HSTRING name, ABI::Windows::Foundation::DateTime value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddDateTimeArray(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::DateTime * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddDateTimeArrayWithFormat(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::DateTime * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddDateTimeArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::DateTime * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddTimeSpan(HSTRING name, ABI::Windows::Foundation::TimeSpan value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddTimeSpanWithFormat(HSTRING name, ABI::Windows::Foundation::TimeSpan value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddTimeSpanWithFormatAndTags(HSTRING name, ABI::Windows::Foundation::TimeSpan value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddTimeSpanArray(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::TimeSpan * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddTimeSpanArrayWithFormat(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::TimeSpan * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddTimeSpanArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::TimeSpan * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddPoint(HSTRING name, ABI::Windows::Foundation::Point value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddPointWithFormat(HSTRING name, ABI::Windows::Foundation::Point value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddPointWithFormatAndTags(HSTRING name, ABI::Windows::Foundation::Point value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddPointArray(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::Point * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddPointArrayWithFormat(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::Point * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddPointArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::Point * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddSize(HSTRING name, ABI::Windows::Foundation::Size value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddSizeWithFormat(HSTRING name, ABI::Windows::Foundation::Size value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddSizeWithFormatAndTags(HSTRING name, ABI::Windows::Foundation::Size value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddSizeArray(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::Size * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddSizeArrayWithFormat(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::Size * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddSizeArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::Size * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddRect(HSTRING name, ABI::Windows::Foundation::Rect value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddRectWithFormat(HSTRING name, ABI::Windows::Foundation::Rect value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddRectWithFormatAndTags(HSTRING name, ABI::Windows::Foundation::Rect value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddRectArray(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::Rect * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddRectArrayWithFormat(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::Rect * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE AddRectArrayWithFormatAndTags(HSTRING name, UINT32 __valueSize, ABI::Windows::Foundation::Rect * value, ABI::Windows::Foundation::Diagnostics::LoggingFieldFormat format, INT32 tags) override { return S_OK; }
};
class LogOptions final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, FD::ILoggingOptions> {
    InspectableClass(L"Windows.Foundation.Diagnostics.LoggingOptions", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE get_Keywords(INT64 * value) override { *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Keywords(INT64 value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Tags(INT32 * value) override { *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Tags(INT32 value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Task(INT16 * value) override { *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Task(INT16 value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Opcode(ABI::Windows::Foundation::Diagnostics::LoggingOpcode * value) override { *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Opcode(ABI::Windows::Foundation::Diagnostics::LoggingOpcode value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ActivityId(GUID * value) override { *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_ActivityId(GUID value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_RelatedActivityId(GUID * value) override { *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_RelatedActivityId(GUID value) override { return S_OK; }
};
class LogChannelOptions final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, FD::ILoggingChannelOptions> {
    InspectableClass(L"Windows.Foundation.Diagnostics.LoggingChannelOptions", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE get_Group(GUID * value) override { *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Group(GUID value) override { return S_OK; }
};
class LogChannel final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, FD::ILoggingChannel, FD::ILoggingChannel2, FD::ILoggingTarget, ABI::Windows::Foundation::IClosable> {
    InspectableClass(L"Windows.Foundation.Diagnostics.LoggingChannel", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING * value) override { *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Enabled(boolean * value) override { *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Level(ABI::Windows::Foundation::Diagnostics::LoggingLevel * value) override { *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE LogMessage(HSTRING eventString) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE LogMessageWithLevel(HSTRING eventString, ABI::Windows::Foundation::Diagnostics::LoggingLevel level) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE LogValuePair(HSTRING value1, INT32 value2) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE LogValuePairWithLevel(HSTRING value1, INT32 value2, ABI::Windows::Foundation::Diagnostics::LoggingLevel level) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_LoggingEnabled(__FITypedEventHandler_2_Windows__CFoundation__CDiagnostics__CILoggingChannel_IInspectable * handler, EventRegistrationToken * token) override { static INT64 n = 1; token->value = n++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_LoggingEnabled(EventRegistrationToken token) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Id(GUID * value) override { *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE IsEnabled(boolean * result) override { *result = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE IsEnabledWithLevel(ABI::Windows::Foundation::Diagnostics::LoggingLevel level, boolean * result) override { *result = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE IsEnabledWithLevelAndKeywords(ABI::Windows::Foundation::Diagnostics::LoggingLevel level, INT64 keywords, boolean * result) override { *result = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE LogEvent(HSTRING eventName) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE LogEventWithFields(HSTRING eventName, ABI::Windows::Foundation::Diagnostics::ILoggingFields * fields) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE LogEventWithFieldsAndLevel(HSTRING eventName, ABI::Windows::Foundation::Diagnostics::ILoggingFields * fields, ABI::Windows::Foundation::Diagnostics::LoggingLevel level) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE LogEventWithFieldsAndOptions(HSTRING eventName, ABI::Windows::Foundation::Diagnostics::ILoggingFields * fields, ABI::Windows::Foundation::Diagnostics::LoggingLevel level, ABI::Windows::Foundation::Diagnostics::ILoggingOptions * options) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE StartActivity(HSTRING startEventName, ABI::Windows::Foundation::Diagnostics::ILoggingActivity * * result) override { *result = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE StartActivityWithFields(HSTRING startEventName, ABI::Windows::Foundation::Diagnostics::ILoggingFields * fields, ABI::Windows::Foundation::Diagnostics::ILoggingActivity * * result) override { *result = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE StartActivityWithFieldsAndLevel(HSTRING startEventName, ABI::Windows::Foundation::Diagnostics::ILoggingFields * fields, ABI::Windows::Foundation::Diagnostics::LoggingLevel level, ABI::Windows::Foundation::Diagnostics::ILoggingActivity * * result) override { *result = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE StartActivityWithFieldsAndOptions(HSTRING startEventName, ABI::Windows::Foundation::Diagnostics::ILoggingFields * fields, ABI::Windows::Foundation::Diagnostics::LoggingLevel level, ABI::Windows::Foundation::Diagnostics::ILoggingOptions * options, ABI::Windows::Foundation::Diagnostics::ILoggingActivity * * result) override { *result = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE Close(void) override { return S_OK; }
};
class LogOptionsFactory final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, FD::ILoggingOptionsFactory> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { HRESULT h = RuntimeClass::QueryInterface(riid, ppv); if (FAILED(h)) CompatMissQI("LogOptionsFactory", riid); return h; }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { return Make<LogOptions>().CopyTo(out); }
    HRESULT STDMETHODCALLTYPE CreateWithKeywords(INT64 keywords, ABI::Windows::Foundation::Diagnostics::ILoggingOptions * * result) override { return Make<LogOptions>().CopyTo(result); }
};
class LogChannelOptionsFactory final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, FD::ILoggingChannelOptionsFactory> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { HRESULT h = RuntimeClass::QueryInterface(riid, ppv); if (FAILED(h)) CompatMissQI("LogChannelOptionsFactory", riid); return h; }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { return Make<LogChannelOptions>().CopyTo(out); }
    HRESULT STDMETHODCALLTYPE Create(GUID group, ABI::Windows::Foundation::Diagnostics::ILoggingChannelOptions * * result) override { return Make<LogChannelOptions>().CopyTo(result); }
};
class LogChannelFactory final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, FD::ILoggingChannelFactory, FD::ILoggingChannelFactory2> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { HRESULT h = RuntimeClass::QueryInterface(riid, ppv); if (FAILED(h)) CompatMissQI("LogChannelFactory", riid); return h; }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { if (out) *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Create(HSTRING name, ABI::Windows::Foundation::Diagnostics::ILoggingChannel * * result) override { return Make<LogChannel>().CopyTo(result); }
    HRESULT STDMETHODCALLTYPE CreateWithOptions(HSTRING name, ABI::Windows::Foundation::Diagnostics::ILoggingChannelOptions * options, ABI::Windows::Foundation::Diagnostics::ILoggingChannel * * result) override { return Make<LogChannel>().CopyTo(result); }
    HRESULT STDMETHODCALLTYPE CreateWithOptionsAndId(HSTRING name, ABI::Windows::Foundation::Diagnostics::ILoggingChannelOptions * options, GUID id, ABI::Windows::Foundation::Diagnostics::ILoggingChannel * * result) override { return Make<LogChannel>().CopyTo(result); }
};
class LogFieldsFactory final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory> { public: HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { return Make<LogFields>().CopyTo(out); } };
namespace GL = ABI::Windows::Devices::Geolocation;
class Locator final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, GL::IGeolocator> {
    InspectableClass(L"Windows.Devices.Geolocation.Geolocator", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE get_DesiredAccuracy(ABI::Windows::Devices::Geolocation::PositionAccuracy * value) override { CompatMiss("IGeolocator.get_DesiredAccuracy (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_DesiredAccuracy(ABI::Windows::Devices::Geolocation::PositionAccuracy value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_MovementThreshold(DOUBLE * value) override { CompatMiss("IGeolocator.get_MovementThreshold (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_MovementThreshold(DOUBLE value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ReportInterval(UINT32 * value) override { CompatMiss("IGeolocator.get_ReportInterval (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_ReportInterval(UINT32 value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_LocationStatus(ABI::Windows::Devices::Geolocation::PositionStatus * value) override { *value = GL::PositionStatus_NotAvailable; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetGeopositionAsync(__FIAsyncOperation_1_Windows__CDevices__CGeolocation__CGeoposition * * value) override { CompatMiss("IGeolocator.GetGeopositionAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetGeopositionAsyncWithAgeAndTimeout(ABI::Windows::Foundation::TimeSpan maximumAge, ABI::Windows::Foundation::TimeSpan timeout, __FIAsyncOperation_1_Windows__CDevices__CGeolocation__CGeoposition * * value) override { CompatMiss("IGeolocator.GetGeopositionAsyncWithAgeAndTimeout"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE add_PositionChanged(__FITypedEventHandler_2_Windows__CDevices__CGeolocation__CGeolocator_Windows__CDevices__CGeolocation__CPositionChangedEventArgs * handler, EventRegistrationToken * token) override { static INT64 n = 1; token->value = n++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_PositionChanged(EventRegistrationToken token) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_StatusChanged(__FITypedEventHandler_2_Windows__CDevices__CGeolocation__CGeolocator_Windows__CDevices__CGeolocation__CStatusChangedEventArgs * handler, EventRegistrationToken * token) override { static INT64 n = 1; token->value = n++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_StatusChanged(EventRegistrationToken token) override { return S_OK; }
};
class LocatorStatics final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, GL::IGeolocatorStatics> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { HRESULT h = RuntimeClass::QueryInterface(riid, ppv); if (FAILED(h)) CompatMissQI("LocatorStatics", riid); return h; }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { return Make<Locator>().CopyTo(out); }
    HRESULT STDMETHODCALLTYPE RequestAccessAsync(__FIAsyncOperation_1_Windows__CDevices__CGeolocation__CGeolocationAccessStatus * * result) override { return Make<DoneValue<GL::GeolocationAccessStatus>>(GL::GeolocationAccessStatus_Denied).CopyTo(result); }
    HRESULT STDMETHODCALLTYPE GetGeopositionHistoryAsync(ABI::Windows::Foundation::DateTime startTime, __FIAsyncOperation_1___FIVectorView_1_Windows__CDevices__CGeolocation__CGeoposition * * result) override { CompatMiss("IGeolocatorStatics.GetGeopositionHistoryAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetGeopositionHistoryWithDurationAsync(ABI::Windows::Foundation::DateTime startTime, ABI::Windows::Foundation::TimeSpan duration, __FIAsyncOperation_1___FIVectorView_1_Windows__CDevices__CGeolocation__CGeoposition * * result) override { CompatMiss("IGeolocatorStatics.GetGeopositionHistoryWithDurationAsync"); return E_NOTIMPL; }
};
namespace SY = ABI::Windows::System;
class UserPickerObj final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, SY::IUserPicker> {
    InspectableClass(L"Windows.System.UserPicker", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE get_AllowGuestAccounts(boolean * value) override { CompatMiss("IUserPicker.get_AllowGuestAccounts (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_AllowGuestAccounts(boolean value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_SuggestedSelectedUser(ABI::Windows::System::IUser * * value) override { CompatMiss("IUserPicker.get_SuggestedSelectedUser (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_SuggestedSelectedUser(ABI::Windows::System::IUser * value) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PickSingleUserAsync(__FIAsyncOperation_1_Windows__CSystem__CUser * * pickSingleUserOperation) override { CompatMiss("IUserPicker.PickSingleUserAsync"); return E_NOTIMPL; }
};
class UserPickerStatics final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, SY::IUserPickerStatics> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { HRESULT h = RuntimeClass::QueryInterface(riid, ppv); if (FAILED(h)) CompatMissQI("UserPickerStatics", riid); return h; }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { return Make<UserPickerObj>().CopyTo(out); }
    HRESULT STDMETHODCALLTYPE IsSupported(boolean * value) override { *value = false; return S_OK; }
};
class OfflineUser final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, SY::IUser> {
    InspectableClass(L"Windows.System.User", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE get_NonRoamableId(HSTRING * value) override { return Str(L"halo5campaign-offline-user", value); }
    HRESULT STDMETHODCALLTYPE get_AuthenticationStatus(ABI::Windows::System::UserAuthenticationStatus * value) override { *value = SY::UserAuthenticationStatus_LocallyAuthenticated; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Type(ABI::Windows::System::UserType * value) override { *value = SY::UserType_LocalUser; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPropertyAsync(HSTRING value, __FIAsyncOperation_1_IInspectable * * operation) override { LogText("User.GetPropertyAsync", value); return Make<Done<IInspectable*, IInspectable>>(nullptr).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE GetPropertiesAsync(__FIVectorView_1_HSTRING * values, __FIAsyncOperation_1___F__CIPropertySet * * operation) override { CompatMiss("IUser.GetPropertiesAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPictureAsync(ABI::Windows::System::UserPictureSize desiredSize, __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamReference * * operation) override { CompatMiss("IUser.GetPictureAsync"); return E_NOTIMPL; }
};
class UserStatics final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, SY::IUserStatics> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { HRESULT h = RuntimeClass::QueryInterface(riid, ppv); if (FAILED(h)) CompatMissQI("UserStatics", riid); return h; }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { if (out) *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateWatcher(ABI::Windows::System::IUserWatcher * * watcher) override { CompatMiss("IUserStatics.CreateWatcher"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindAllAsync(__FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser * * operation) override { return Make<Done<ABI::Windows::Foundation::Collections::IVectorView<SY::User*>*, ABI::Windows::Foundation::Collections::IVectorView<SY::User*>>>(Make<OneView<SY::User*>>(TheUser().Get()).Get()).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE FindAllAsyncByType(ABI::Windows::System::UserType type, __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser * * operation) override { return Make<Done<ABI::Windows::Foundation::Collections::IVectorView<SY::User*>*, ABI::Windows::Foundation::Collections::IVectorView<SY::User*>>>(Make<OneView<SY::User*>>(TheUser().Get()).Get()).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE FindAllAsyncByTypeAndStatus(ABI::Windows::System::UserType type, ABI::Windows::System::UserAuthenticationStatus status, __FIAsyncOperation_1___FIVectorView_1_Windows__CSystem__CUser * * operation) override { return Make<Done<ABI::Windows::Foundation::Collections::IVectorView<SY::User*>*, ABI::Windows::Foundation::Collections::IVectorView<SY::User*>>>(Make<OneView<SY::User*>>(TheUser().Get()).Get()).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE GetFromId(HSTRING nonRoamableId, ABI::Windows::System::IUser * * value) override { return TheUser().CopyTo(value); }
};
class PushStatics final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, ABI::Windows::Networking::PushNotifications::IPushNotificationChannelManagerStatics> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { HRESULT h = RuntimeClass::QueryInterface(riid, ppv); if (FAILED(h)) CompatMissQI("PushStatics", riid); return h; }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { if (out) *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreatePushNotificationChannelForApplicationAsync(__FIAsyncOperation_1_Windows__CNetworking__CPushNotifications__CPushNotificationChannel * * operation) override { return Make<Failed<ABI::Windows::Networking::PushNotifications::PushNotificationChannel*, ABI::Windows::Networking::PushNotifications::IPushNotificationChannel>>(HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE)).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE CreatePushNotificationChannelForApplicationAsyncWithId(HSTRING applicationId, __FIAsyncOperation_1_Windows__CNetworking__CPushNotifications__CPushNotificationChannel * * operation) override { return Make<Failed<ABI::Windows::Networking::PushNotifications::PushNotificationChannel*, ABI::Windows::Networking::PushNotifications::IPushNotificationChannel>>(HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE)).CopyTo(operation); }
    HRESULT STDMETHODCALLTYPE CreatePushNotificationChannelForSecondaryTileAsync(HSTRING tileId, __FIAsyncOperation_1_Windows__CNetworking__CPushNotifications__CPushNotificationChannel * * operation) override { return Make<Failed<ABI::Windows::Networking::PushNotifications::PushNotificationChannel*, ABI::Windows::Networking::PushNotifications::IPushNotificationChannel>>(HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE)).CopyTo(operation); }
};
namespace VM = ABI::Windows::UI::ViewManagement;
class UiSettings final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, VM::IUISettings, VM::IUISettings2, VM::IUISettings3, VM::IUISettings4> {
    InspectableClass(L"Windows.UI.ViewManagement.UISettings", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE get_HandPreference(ABI::Windows::UI::ViewManagement::HandPreference * value) override { *value = VM::HandPreference_RightHanded; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CursorSize(ABI::Windows::Foundation::Size * value) override { value->Width = 32; value->Height = 32; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ScrollBarSize(ABI::Windows::Foundation::Size * value) override { value->Width = 17; value->Height = 17; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ScrollBarArrowSize(ABI::Windows::Foundation::Size * value) override { value->Width = 17; value->Height = 17; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ScrollBarThumbBoxSize(ABI::Windows::Foundation::Size * value) override { value->Width = 17; value->Height = 17; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_MessageDuration(UINT32 * value) override { *value = 5; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AnimationsEnabled(boolean * value) override { *value = true; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CaretBrowsingEnabled(boolean * value) override { *value = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CaretBlinkRate(UINT32 * value) override { *value = GetCaretBlinkTime(); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CaretWidth(UINT32 * value) override { *value = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_DoubleClickTime(UINT32 * value) override { *value = GetDoubleClickTime(); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_MouseHoverTime(UINT32 * value) override { *value = 400; return S_OK; }
    HRESULT STDMETHODCALLTYPE UIElementColor(ABI::Windows::UI::ViewManagement::UIElementType desiredElement, ABI::Windows::UI::Color * value) override { COLORREF c = GetSysColor(COLOR_WINDOWTEXT); *value = { 255, GetRValue(c), GetGValue(c), GetBValue(c) }; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_TextScaleFactor(DOUBLE * value) override { *value = 1.0; return S_OK; }
    HRESULT STDMETHODCALLTYPE add_TextScaleFactorChanged(__FITypedEventHandler_2_Windows__CUI__CViewManagement__CUISettings_IInspectable * handler, EventRegistrationToken * cookie) override { static INT64 n = 1; cookie->value = n++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_TextScaleFactorChanged(EventRegistrationToken cookie) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetColorValue(ABI::Windows::UI::ViewManagement::UIColorType desiredColor, ABI::Windows::UI::Color * value) override { switch (desiredColor) { case VM::UIColorType_Background: *value = { 255, 0, 0, 0 }; break; case VM::UIColorType_Foreground: *value = { 255, 255, 255, 255 }; break; default: *value = { 255, 0, 120, 215 }; } return S_OK; }
    HRESULT STDMETHODCALLTYPE add_ColorValuesChanged(__FITypedEventHandler_2_Windows__CUI__CViewManagement__CUISettings_IInspectable * handler, EventRegistrationToken * cookie) override { static INT64 n = 1; cookie->value = n++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_ColorValuesChanged(EventRegistrationToken cookie) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AdvancedEffectsEnabled(boolean * value) override { *value = true; return S_OK; }
    HRESULT STDMETHODCALLTYPE add_AdvancedEffectsEnabledChanged(__FITypedEventHandler_2_Windows__CUI__CViewManagement__CUISettings_IInspectable * handler, EventRegistrationToken * cookie) override { static INT64 n = 1; cookie->value = n++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_AdvancedEffectsEnabledChanged(EventRegistrationToken cookie) override { return S_OK; }
};
namespace NC = ABI::Windows::Networking::Connectivity;
class NetCost final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, NC::IConnectionCost, NC::IConnectionCost2> {
    InspectableClass(L"Windows.Networking.Connectivity.ConnectionCost", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE get_NetworkCostType(ABI::Windows::Networking::Connectivity::NetworkCostType * value) override { *value = NC::NetworkCostType_Unrestricted; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Roaming(boolean * value) override { CompatMiss("IConnectionCost.get_Roaming (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_OverDataLimit(boolean * value) override { CompatMiss("IConnectionCost.get_OverDataLimit (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ApproachingDataLimit(boolean * value) override { CompatMiss("IConnectionCost.get_ApproachingDataLimit (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_BackgroundDataUsageRestricted(boolean * value) override { CompatMiss("IConnectionCost2.get_BackgroundDataUsageRestricted (default value)"); *value = {}; return S_OK; }
};
class NetProfile final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, NC::IConnectionProfile> {
    InspectableClass(L"Windows.Networking.Connectivity.ConnectionProfile", BaseTrust)
public:
    HRESULT STDMETHODCALLTYPE get_ProfileName(HSTRING * value) override { return Str(L"Local", value); }
    HRESULT STDMETHODCALLTYPE GetNetworkConnectivityLevel(ABI::Windows::Networking::Connectivity::NetworkConnectivityLevel * value) override { *value = NC::NetworkConnectivityLevel_InternetAccess; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetNetworkNames(__FIVectorView_1_HSTRING * * value) override { return Make<EmptyView<HSTRING>>().CopyTo(value); }
    HRESULT STDMETHODCALLTYPE GetConnectionCost(ABI::Windows::Networking::Connectivity::IConnectionCost * * value) override { return Make<NetCost>().CopyTo(value); }
    HRESULT STDMETHODCALLTYPE GetDataPlanStatus(ABI::Windows::Networking::Connectivity::IDataPlanStatus * * value) override { CompatMiss("IConnectionProfile.GetDataPlanStatus"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_NetworkAdapter(ABI::Windows::Networking::Connectivity::INetworkAdapter * * value) override { CompatMiss("IConnectionProfile.get_NetworkAdapter (default value)"); *value = {}; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetLocalUsage(ABI::Windows::Foundation::DateTime StartTime, ABI::Windows::Foundation::DateTime EndTime, ABI::Windows::Networking::Connectivity::IDataUsage * * value) override { CompatMiss("IConnectionProfile.GetLocalUsage"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetLocalUsagePerRoamingStates(ABI::Windows::Foundation::DateTime StartTime, ABI::Windows::Foundation::DateTime EndTime, ABI::Windows::Networking::Connectivity::RoamingStates States, ABI::Windows::Networking::Connectivity::IDataUsage * * value) override { CompatMiss("IConnectionProfile.GetLocalUsagePerRoamingStates"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_NetworkSecuritySettings(ABI::Windows::Networking::Connectivity::INetworkSecuritySettings * * value) override { CompatMiss("IConnectionProfile.get_NetworkSecuritySettings (default value)"); *value = {}; return S_OK; }
};
class NetInfoStatics final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, NC::INetworkInformationStatics, NC::INetworkInformationStatics2> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { HRESULT h = RuntimeClass::QueryInterface(riid, ppv); if (FAILED(h)) CompatMissQI("NetInfoStatics", riid); return h; }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { if (out) *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetConnectionProfiles(__FIVectorView_1_Windows__CNetworking__CConnectivity__CConnectionProfile * * value) override { ComPtr<NC::IConnectionProfile> p; Make<NetProfile>().As(&p); return Make<OneView<NC::ConnectionProfile*>>(p.Get()).CopyTo(value); }
    HRESULT STDMETHODCALLTYPE GetInternetConnectionProfile(ABI::Windows::Networking::Connectivity::IConnectionProfile * * value) override { return Make<NetProfile>().CopyTo(value); }
    HRESULT STDMETHODCALLTYPE GetLanIdentifiers(__FIVectorView_1_Windows__CNetworking__CConnectivity__CLanIdentifier * * value) override { return Make<EmptyView<NC::LanIdentifier*>>().CopyTo(value); }
    HRESULT STDMETHODCALLTYPE GetHostNames(__FIVectorView_1_Windows__CNetworking__CHostName * * value) override { return Make<EmptyView<ABI::Windows::Networking::HostName*>>().CopyTo(value); }
    HRESULT STDMETHODCALLTYPE GetProxyConfigurationAsync(ABI::Windows::Foundation::IUriRuntimeClass * uri, __FIAsyncOperation_1_Windows__CNetworking__CConnectivity__CProxyConfiguration * * value) override { CompatMiss("INetworkInformationStatics.GetProxyConfigurationAsync"); return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSortedEndpointPairs(__FIIterable_1_Windows__CNetworking__CEndpointPair * destinationList, ABI::Windows::Networking::HostNameSortOptions sortOptions, __FIVectorView_1_Windows__CNetworking__CEndpointPair * * value) override { return Make<EmptyView<ABI::Windows::Networking::EndpointPair*>>().CopyTo(value); }
    HRESULT STDMETHODCALLTYPE add_NetworkStatusChanged(ABI::Windows::Networking::Connectivity::INetworkStatusChangedEventHandler * networkStatusHandler, EventRegistrationToken * eventCookie) override { static INT64 n = 1; eventCookie->value = n++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_NetworkStatusChanged(EventRegistrationToken eventCookie) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FindConnectionProfilesAsync(ABI::Windows::Networking::Connectivity::IConnectionProfileFilter * pProfileFilter, __FIAsyncOperation_1___FIVectorView_1_Windows__CNetworking__CConnectivity__CConnectionProfile * * value) override { ComPtr<NC::IConnectionProfile> p; Make<NetProfile>().As(&p); return Make<Done<ABI::Windows::Foundation::Collections::IVectorView<NC::ConnectionProfile*>*, ABI::Windows::Foundation::Collections::IVectorView<NC::ConnectionProfile*>>>(Make<OneView<NC::ConnectionProfile*>>(p.Get()).Get()).CopyTo(value); }
};
