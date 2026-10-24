import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); from gen import emit, emit_statics
H='windows.devices.enumeration.h'
print('namespace DE = ABI::Windows::Devices::Enumeration;')
print('using DevView = ABI::Windows::Foundation::Collections::IVectorView<DE::DeviceInformation*>;')
print(emit('DevWatcher','DE::IDeviceWatcher',H,'Windows.Devices.Enumeration.DeviceWatcher',
 extra_members='    ComPtr<ABI::Windows::Foundation::ITypedEventHandler<DE::DeviceWatcher*, IInspectable*>> done_; DE::DeviceWatcherStatus status_ = DE::DeviceWatcherStatus_Created;',
 bodies={
 'add_EnumerationCompleted':'{ done_ = handler; static INT64 n = 1; token->value = n++; return S_OK; }',
 'get_Status':'{ *status = status_; return S_OK; }',
 'Start':'{ status_ = DE::DeviceWatcherStatus_EnumerationCompleted; if (done_) done_->Invoke(this, nullptr); return S_OK; }',
 'Stop':'{ status_ = DE::DeviceWatcherStatus_Stopped; return S_OK; }'}))
empty = '{ return Make<Done<DE::DeviceInformationCollection*, DevView>>(Make<EmptyView<DE::DeviceInformation*>>().Get()).CopyTo(asyncOp); }'
watch = '{ return Make<DevWatcher>().CopyTo(watcher); }'
print(emit_statics('DevInfoStatics', [('DE::IDeviceInformationStatics', H), ('DE::IDeviceInformationStatics2', H)], bodies={
 'FindAllAsync': empty, 'FindAllAsyncDeviceClass': empty, 'FindAllAsyncAqsFilter': empty, 'FindAllAsyncAqsFilterAndAdditionalProperties': empty,
 'FindAllAsyncWithKindAqsFilterAndAdditionalProperties': empty,
 'CreateWatcher': watch, 'CreateWatcherDeviceClass': watch, 'CreateWatcherAqsFilter': watch, 'CreateWatcherAqsFilterAndAdditionalProperties': watch,
 'CreateWatcherWithKindAqsFilterAndAdditionalProperties': watch,
 'GetAqsFilterFromDeviceClass': '{ return Str(L"", aqsFilter); }'}))
