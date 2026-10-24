import sys; sys.path.insert(0, __import__('os').path.dirname(__file__)); from gen import emit
H='windows.services.store.h'
print('namespace S = ABI::Windows::Services::Store;')
print(emit('StoreLicenseInfo','S::IStoreAppLicense',H,'Windows.Services.Store.StoreAppLicense',bodies={
 'get_SkuStoreId':'{ return Str(L"", value); }',
 'get_IsActive':'{ *value = true; return S_OK; }',
 'get_IsTrial':'{ *value = false; return S_OK; }',
 'get_ExpirationDate':'{ value->UniversalTime = 0x7fffffffffffffffLL; return S_OK; }',
 'get_ExtendedJsonData':'{ return Str(L"{}", value); }',
 'get_TrialTimeRemaining':'{ value->Duration = 0; return S_OK; }',
 'get_IsTrialOwnedByThisUser':'{ *value = false; return S_OK; }',
 'get_TrialUniqueId':'{ return Str(L"", value); }',
}))
print(emit('StoreCtx','S::IStoreContext',H,'Windows.Services.Store.StoreContext',extra_members='    INT64 next_ = 1;',bodies={
 'get_User':'{ *value = nullptr; return S_OK; }',
 'add_OfflineLicensesChanged':'{ token->value = next_++; return S_OK; }',
 'remove_OfflineLicensesChanged':'{ return S_OK; }',
 'GetAppAndOptionalStorePackageUpdatesAsync':'{ return Make<Done<ABI::Windows::Foundation::Collections::IVectorView<S::StorePackageUpdate*>*, ABI::Windows::Foundation::Collections::IVectorView<S::StorePackageUpdate*>>>(Make<EmptyView<S::StorePackageUpdate*>>().Get()).CopyTo(operation); }',
 'GetAppLicenseAsync':'{ CompatNote("StoreContext.GetAppLicenseAsync (offline: full licence)"); return Make<Done<S::StoreAppLicense*, S::IStoreAppLicense>>(Make<StoreLicenseInfo>().Get()).CopyTo(operation); }',
}))
print('''class StoreCtxStatics final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, S::IStoreContextStatics> {
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { if (out) *out = nullptr; return E_NOTIMPL; }''')
from gen import methods
for n,p in methods(H,'IStoreContextStatics'):
    print(f'    HRESULT STDMETHODCALLTYPE {n}({p}) override {{ return Make<StoreCtx>().CopyTo(value); }}')
print('};')
