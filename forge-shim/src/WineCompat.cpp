// Windows Runtime classes Wine (the PS5's prospero-win included) is missing or only partly has. Each class here is
// used only when the system's own factory is missing or lacks what the game uses, so Windows keeps its real classes.
#include <windows.h>
#include <roapi.h>
#include <wrl.h>
#include <windows.globalization.h>
#include <windows.applicationmodel.extendedexecution.h>
#include <windows.foundation.collections.h>
#include <windows.ui.core.h>
#include <windows.services.store.h>
#include <windows.system.h>
#include <windows.devices.enumeration.h>
#include <windows.ui.popups.h>
#include <windows.foundation.diagnostics.h>
#include <windows.devices.geolocation.h>
#include <windows.system.threading.h>
#include <windows.networking.pushnotifications.h>
#include <atomic>
#include <windows.ui.viewmanagement.h>
#include <windows.networking.connectivity.h>
#include <set>
#include <msctf.h>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <type_traits>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Foundation;
using RealFactory = HRESULT(WINAPI*)(HSTRING, REFIID, void**);

namespace {
HRESULT Str(const wchar_t* s, HSTRING* out) { if (!out) return E_POINTER; return WindowsCreateString(s, static_cast<UINT32>(wcslen(s)), out); }
void Note(const char* what) { char l[160]; sprintf_s(l, "[ForgeShim] compat: %s\n", what); OutputDebugStringA(l); }
void CompatNote(const char* what) { Note(what); }
// An interface the game asked a stand-in class for that it doesn't have (once per class and interface).
void CompatMissQI(const char* cls, REFIID iid)
{
    wchar_t g[64]; StringFromGUID2(iid, g, 64); char what[160]; sprintf_s(what, "%s has no %ls", cls, g);
    static std::mutex m; static std::set<std::string> seen;
    { std::lock_guard<std::mutex> lk(m); if (!seen.insert(what).second) return; }
    char l[220]; sprintf_s(l, "[ForgeShim] compat: %s\n", what); OutputDebugStringA(l);
}
// A method of a stand-in class the game called but that isn't implemented (once per method).
void CompatMiss(const char* what)
{
    static std::mutex m; static std::set<std::string> seen;
    { std::lock_guard<std::mutex> g(m); if (!seen.insert(what).second) return; }
    char l[200]; sprintf_s(l, "[ForgeShim] compat: not implemented %s\n", what); OutputDebugStringA(l);
}
// The real factory, when it exists and answers the interface the game needs.
bool RealHas(RealFactory real, HSTRING name, REFIID need, REFIID iid, void** out)
{
    ComPtr<IActivationFactory> f; if (FAILED(real(name, IID_PPV_ARGS(&f))) || !f) return false;
    ComPtr<IUnknown> probe; if (FAILED(f.As(&probe)) || FAILED(f->QueryInterface(need, &probe))) return false;
    return SUCCEEDED(f->QueryInterface(iid, out));
}

// ---- completed async operations ------------------------------------------------------------------
template <typename T>
class DoneValue : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IAsyncOperation<T>, IAsyncInfo> {
    using A = typename ABI::Windows::Foundation::Internal::GetAbiType<typename IAsyncOperation<T>::TResult_complex>::type;
    A value_; ComPtr<IAsyncOperationCompletedHandler<T>> handler_;
public:
    explicit DoneValue(A v) : value_(v) {}
    HRESULT STDMETHODCALLTYPE put_Completed(IAsyncOperationCompletedHandler<T>* h) override { handler_ = h; return h ? h->Invoke(this, AsyncStatus::Completed) : S_OK; }
    HRESULT STDMETHODCALLTYPE get_Completed(IAsyncOperationCompletedHandler<T>** h) override { return handler_.CopyTo(h); }
    HRESULT STDMETHODCALLTYPE GetResults(A* r) override { if (!r) return E_POINTER; *r = value_; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Id(unsigned* id) override { *id = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Status(AsyncStatus* s) override { *s = AsyncStatus::Completed; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* e) override { *e = S_OK; return S_OK; }
    HRESULT STDMETHODCALLTYPE Cancel() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
template <typename TRuntime, typename TAbi>
class Done : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IAsyncOperation<TRuntime>, IAsyncInfo> {
    ComPtr<TAbi> value_; ComPtr<IAsyncOperationCompletedHandler<TRuntime>> handler_;
public:
    explicit Done(TAbi* v) : value_(v) {}
    HRESULT STDMETHODCALLTYPE put_Completed(IAsyncOperationCompletedHandler<TRuntime>* h) override { handler_ = h; return h ? h->Invoke(this, AsyncStatus::Completed) : S_OK; }
    HRESULT STDMETHODCALLTYPE get_Completed(IAsyncOperationCompletedHandler<TRuntime>** h) override { return handler_.CopyTo(h); }
    HRESULT STDMETHODCALLTYPE GetResults(TAbi** r) override { if (!r) return E_POINTER; return value_.CopyTo(r); }
    HRESULT STDMETHODCALLTYPE get_Id(unsigned* id) override { *id = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Status(AsyncStatus* s) override { *s = AsyncStatus::Completed; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* e) override { *e = S_OK; return S_OK; }
    HRESULT STDMETHODCALLTYPE Cancel() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
// An operation that has already failed with hr (the game's task continuation sees the error).
template <typename TRuntime, typename TAbi>
class Failed : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IAsyncOperation<TRuntime>, IAsyncInfo> {
    HRESULT hr_; ComPtr<IAsyncOperationCompletedHandler<TRuntime>> handler_;
public:
    explicit Failed(HRESULT hr) : hr_(hr) {}
    HRESULT STDMETHODCALLTYPE put_Completed(IAsyncOperationCompletedHandler<TRuntime>* h) override { handler_ = h; return h ? h->Invoke(this, AsyncStatus::Error) : S_OK; }
    HRESULT STDMETHODCALLTYPE get_Completed(IAsyncOperationCompletedHandler<TRuntime>** h) override { return handler_.CopyTo(h); }
    HRESULT STDMETHODCALLTYPE GetResults(TAbi** r) override { if (r) *r = nullptr; return hr_; }
    HRESULT STDMETHODCALLTYPE get_Id(unsigned* id) override { *id = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Status(AsyncStatus* s) override { *s = AsyncStatus::Error; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* e) override { *e = hr_; return S_OK; }
    HRESULT STDMETHODCALLTYPE Cancel() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};
// A read-only list of one item.
template <typename T>
class OneView final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, ABI::Windows::Foundation::Collections::IVectorView<T>> {
    using A = typename ABI::Windows::Foundation::Internal::GetAbiType<typename ABI::Windows::Foundation::Collections::IVectorView<T>::T_complex>::type;
    ComPtr<std::remove_pointer_t<A>> item_;
public:
    explicit OneView(A item) : item_(item) {}
    HRESULT STDMETHODCALLTYPE GetAt(unsigned i, A* v) override { if (i) return E_BOUNDS; return item_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* n) override { *n = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE IndexOf(A v, unsigned* i, boolean* found) override { *i = 0; *found = v == item_.Get(); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMany(unsigned start, unsigned cap, A* v, unsigned* n) override { *n = 0; if (!start && cap) { item_.CopyTo(&v[0]); *n = 1; } return S_OK; }
};
ComPtr<ABI::Windows::System::IUser> TheUser();
// Empty collections (results the offline game gets nothing in).
template <typename T>
class EmptyIterator final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, ABI::Windows::Foundation::Collections::IIterator<T>> {
    using A = typename ABI::Windows::Foundation::Internal::GetAbiType<typename ABI::Windows::Foundation::Collections::IIterator<T>::T_complex>::type;
public:
    HRESULT STDMETHODCALLTYPE get_Current(A*) override { return E_BOUNDS; }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* v) override { *v = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* v) override { *v = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMany(unsigned, A*, unsigned* n) override { *n = 0; return S_OK; }
};
template <typename T>
class EmptyView final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, ABI::Windows::Foundation::Collections::IVectorView<T>, ABI::Windows::Foundation::Collections::IIterable<T>> {
    using A = typename ABI::Windows::Foundation::Internal::GetAbiType<typename ABI::Windows::Foundation::Collections::IVectorView<T>::T_complex>::type;
public:
    HRESULT STDMETHODCALLTYPE GetAt(unsigned, A*) override { return E_BOUNDS; }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* n) override { *n = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE IndexOf(A, unsigned* i, boolean* found) override { *i = 0; *found = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMany(unsigned, unsigned, A*, unsigned* n) override { *n = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE First(ABI::Windows::Foundation::Collections::IIterator<T>** v) override { return Make<EmptyIterator<T>>().CopyTo(v); }
};
// A vector of interface pointers.
template <typename T>
class Vector final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, ABI::Windows::Foundation::Collections::IVector<T>, ABI::Windows::Foundation::Collections::IIterable<T>> {
    using A = typename ABI::Windows::Foundation::Internal::GetAbiType<typename ABI::Windows::Foundation::Collections::IVector<T>::T_complex>::type;
    using I = std::remove_pointer_t<A>;
public:
    std::vector<ComPtr<I>> items;
    HRESULT STDMETHODCALLTYPE GetAt(unsigned i, A* v) override { if (i >= items.size()) return E_BOUNDS; return items[i].CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* n) override { *n = static_cast<unsigned>(items.size()); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetView(ABI::Windows::Foundation::Collections::IVectorView<T>** v) override { *v = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE IndexOf(A v, unsigned* i, boolean* found) override { for (unsigned k = 0; k < items.size(); ++k) if (items[k].Get() == v) { *i = k; *found = true; return S_OK; } *i = 0; *found = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetAt(unsigned i, A v) override { if (i >= items.size()) return E_BOUNDS; items[i] = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE InsertAt(unsigned i, A v) override { if (i > items.size()) return E_BOUNDS; items.insert(items.begin() + i, ComPtr<I>(v)); return S_OK; }
    HRESULT STDMETHODCALLTYPE RemoveAt(unsigned i) override { if (i >= items.size()) return E_BOUNDS; items.erase(items.begin() + i); return S_OK; }
    HRESULT STDMETHODCALLTYPE Append(A v) override { items.emplace_back(v); return S_OK; }
    HRESULT STDMETHODCALLTYPE RemoveAtEnd() override { if (items.empty()) return E_BOUNDS; items.pop_back(); return S_OK; }
    HRESULT STDMETHODCALLTYPE Clear() override { items.clear(); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMany(unsigned start, unsigned cap, A* v, unsigned* n) override { unsigned k = 0; for (; k < cap && start + k < items.size(); ++k) items[start + k].CopyTo(&v[k]); *n = k; return S_OK; }
    HRESULT STDMETHODCALLTYPE ReplaceAll(unsigned count, A* v) override { items.clear(); for (unsigned k = 0; k < count; ++k) items.emplace_back(v[k]); return S_OK; }
    HRESULT STDMETHODCALLTYPE First(ABI::Windows::Foundation::Collections::IIterator<T>** v) override { *v = nullptr; return E_NOTIMPL; }
};
// A factory whose ActivateInstance makes T (for classes the game creates with "ref new").
template <typename T>
class Activatable final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory> {
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { if (!out) return E_POINTER; return Make<T>().CopyTo(out); }
};

// ---- Windows.Foundation.Collections.PropertySet ----------------------------------------------------
namespace C = ABI::Windows::Foundation::Collections;
using Pair = C::IKeyValuePair<HSTRING, IInspectable*>;
using Entries = std::vector<std::pair<std::wstring, ComPtr<IInspectable>>>;
class KeyValue final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Pair> {
    std::wstring key_; ComPtr<IInspectable> value_;
public:
    KeyValue(std::wstring k, ComPtr<IInspectable> v) : key_(std::move(k)), value_(std::move(v)) {}
    HRESULT STDMETHODCALLTYPE get_Key(HSTRING* k) override { return Str(key_.c_str(), k); }
    HRESULT STDMETHODCALLTYPE get_Value(IInspectable** v) override { if (!v) return E_POINTER; return value_.CopyTo(v); }
};
class PairIterator final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, C::IIterator<Pair*>> {
    Entries items_; size_t at_ = 0;
public:
    explicit PairIterator(Entries items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE get_Current(Pair** v) override { if (!v) return E_POINTER; if (at_ >= items_.size()) return E_BOUNDS; return Make<KeyValue>(items_[at_].first, items_[at_].second).CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* v) override { *v = at_ < items_.size(); return S_OK; }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* v) override { if (at_ < items_.size()) ++at_; *v = at_ < items_.size(); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMany(unsigned cap, Pair** items, unsigned* got) override
    { unsigned n = 0; for (; n < cap && at_ < items_.size(); ++n, ++at_) Make<KeyValue>(items_[at_].first, items_[at_].second).CopyTo(&items[n]); *got = n; return S_OK; }
};
class PropertyView final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, C::IMapView<HSTRING, IInspectable*>, C::IIterable<Pair*>> {
    Entries items_;
    const ComPtr<IInspectable>* Find(HSTRING k) const { const wchar_t* key = WindowsGetStringRawBuffer(k, nullptr); for (auto& e : items_) if (e.first == key) return std::addressof(e.second); return nullptr; }
public:
    explicit PropertyView(Entries items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE Lookup(HSTRING k, IInspectable** v) override { auto f = Find(k); if (!f) return E_BOUNDS; return f->CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* n) override { *n = static_cast<unsigned>(items_.size()); return S_OK; }
    HRESULT STDMETHODCALLTYPE HasKey(HSTRING k, boolean* v) override { *v = Find(k) != nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE Split(C::IMapView<HSTRING, IInspectable*>** a, C::IMapView<HSTRING, IInspectable*>** b) override { *a = nullptr; *b = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE First(C::IIterator<Pair*>** v) override { return Make<PairIterator>(items_).CopyTo(v); }
};
class PropertySet final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, C::IPropertySet, C::IObservableMap<HSTRING, IInspectable*>,
    C::IMap<HSTRING, IInspectable*>, C::IIterable<Pair*>> {
    InspectableClass(L"Windows.Foundation.Collections.PropertySet", BaseTrust)
    std::map<std::wstring, ComPtr<IInspectable>> map_; std::mutex lock_; INT64 next_ = 1;
    Entries Snapshot() { std::lock_guard<std::mutex> g(lock_); Entries e; for (auto& kv : map_) e.push_back(kv); return e; }
public:
    HRESULT STDMETHODCALLTYPE Lookup(HSTRING k, IInspectable** v) override
    { std::lock_guard<std::mutex> g(lock_); auto it = map_.find(WindowsGetStringRawBuffer(k, nullptr)); if (it == map_.end()) return E_BOUNDS; return it->second.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* n) override { std::lock_guard<std::mutex> g(lock_); *n = static_cast<unsigned>(map_.size()); return S_OK; }
    HRESULT STDMETHODCALLTYPE HasKey(HSTRING k, boolean* v) override { std::lock_guard<std::mutex> g(lock_); *v = map_.count(WindowsGetStringRawBuffer(k, nullptr)) != 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetView(C::IMapView<HSTRING, IInspectable*>** v) override { return Make<PropertyView>(Snapshot()).CopyTo(v); }
    HRESULT STDMETHODCALLTYPE Insert(HSTRING k, IInspectable* v, boolean* replaced) override
    { std::lock_guard<std::mutex> g(lock_); std::wstring key = WindowsGetStringRawBuffer(k, nullptr); if (replaced) *replaced = map_.count(key) != 0; map_[key] = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE Remove(HSTRING k) override { std::lock_guard<std::mutex> g(lock_); return map_.erase(WindowsGetStringRawBuffer(k, nullptr)) ? S_OK : E_BOUNDS; }
    HRESULT STDMETHODCALLTYPE Clear() override { std::lock_guard<std::mutex> g(lock_); map_.clear(); return S_OK; }
    HRESULT STDMETHODCALLTYPE First(C::IIterator<Pair*>** v) override { return Make<PairIterator>(Snapshot()).CopyTo(v); }
    HRESULT STDMETHODCALLTYPE add_MapChanged(C::MapChangedEventHandler<HSTRING, IInspectable*>*, EventRegistrationToken* t) override { t->value = next_++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_MapChanged(EventRegistrationToken) override { return S_OK; }
};

// ---- Windows.Foundation.Collections.StringMap (Wine has none; the shim's offline sign-in builds its token requests
// and responses on it) ------------------------------------------------------------------------------------------------
using SPair = C::IKeyValuePair<HSTRING, HSTRING>;
using SEntries = std::vector<std::pair<std::wstring, std::wstring>>;
class StringKeyValue final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, SPair> {
    std::wstring key_, value_;
public:
    StringKeyValue(std::wstring k, std::wstring v) : key_(std::move(k)), value_(std::move(v)) {}
    HRESULT STDMETHODCALLTYPE get_Key(HSTRING* k) override { return Str(key_.c_str(), k); }
    HRESULT STDMETHODCALLTYPE get_Value(HSTRING* v) override { return Str(value_.c_str(), v); }
};
class StringPairIterator final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, C::IIterator<SPair*>> {
    SEntries items_; size_t at_ = 0;
public:
    explicit StringPairIterator(SEntries items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE get_Current(SPair** v) override { if (!v) return E_POINTER; if (at_ >= items_.size()) return E_BOUNDS; return Make<StringKeyValue>(items_[at_].first, items_[at_].second).CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* v) override { *v = at_ < items_.size(); return S_OK; }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* v) override { if (at_ < items_.size()) ++at_; *v = at_ < items_.size(); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMany(unsigned cap, SPair** items, unsigned* got) override
    { unsigned n = 0; for (; n < cap && at_ < items_.size(); ++n, ++at_) Make<StringKeyValue>(items_[at_].first, items_[at_].second).CopyTo(&items[n]); *got = n; return S_OK; }
};
class StringMapView final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, C::IMapView<HSTRING, HSTRING>, C::IIterable<SPair*>> {
    SEntries items_;
    const std::wstring* Find(HSTRING k) const { const wchar_t* key = WindowsGetStringRawBuffer(k, nullptr); for (auto& e : items_) if (e.first == key) return &e.second; return nullptr; }
public:
    explicit StringMapView(SEntries items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE Lookup(HSTRING k, HSTRING* v) override { auto f = Find(k); if (!f) return E_BOUNDS; return Str(f->c_str(), v); }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* n) override { *n = static_cast<unsigned>(items_.size()); return S_OK; }
    HRESULT STDMETHODCALLTYPE HasKey(HSTRING k, boolean* v) override { *v = Find(k) != nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE Split(C::IMapView<HSTRING, HSTRING>** a, C::IMapView<HSTRING, HSTRING>** b) override { *a = nullptr; *b = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE First(C::IIterator<SPair*>** v) override { return Make<StringPairIterator>(items_).CopyTo(v); }
};
class StringMap final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, C::IMap<HSTRING, HSTRING>, C::IObservableMap<HSTRING, HSTRING>, C::IIterable<SPair*>> {
    InspectableClass(L"Windows.Foundation.Collections.StringMap", BaseTrust)
    std::map<std::wstring, std::wstring> map_; std::mutex lock_; INT64 next_ = 1;
    SEntries Snapshot() { std::lock_guard<std::mutex> g(lock_); SEntries e; for (auto& kv : map_) e.push_back(kv); return e; }
    static std::wstring Key(HSTRING k) { UINT32 n = 0; auto p = WindowsGetStringRawBuffer(k, &n); return std::wstring(p ? p : L"", n); }
public:
    HRESULT STDMETHODCALLTYPE Lookup(HSTRING k, HSTRING* v) override
    { std::lock_guard<std::mutex> g(lock_); auto it = map_.find(Key(k)); if (it == map_.end()) return E_BOUNDS; return WindowsCreateString(it->second.c_str(), static_cast<UINT32>(it->second.size()), v); }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* n) override { std::lock_guard<std::mutex> g(lock_); *n = static_cast<unsigned>(map_.size()); return S_OK; }
    HRESULT STDMETHODCALLTYPE HasKey(HSTRING k, boolean* v) override { std::lock_guard<std::mutex> g(lock_); *v = map_.count(Key(k)) != 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetView(C::IMapView<HSTRING, HSTRING>** v) override { return Make<StringMapView>(Snapshot()).CopyTo(v); }
    HRESULT STDMETHODCALLTYPE Insert(HSTRING k, HSTRING v, boolean* replaced) override
    { std::lock_guard<std::mutex> g(lock_); auto key = Key(k); bool had = map_.count(key) != 0; map_[key] = Key(v); if (replaced) *replaced = had; return S_OK; }
    HRESULT STDMETHODCALLTYPE Remove(HSTRING k) override { std::lock_guard<std::mutex> g(lock_); return map_.erase(Key(k)) ? S_OK : E_BOUNDS; }
    HRESULT STDMETHODCALLTYPE Clear() override { std::lock_guard<std::mutex> g(lock_); map_.clear(); return S_OK; }
    HRESULT STDMETHODCALLTYPE First(C::IIterator<SPair*>** v) override { return Make<StringPairIterator>(Snapshot()).CopyTo(v); }
    HRESULT STDMETHODCALLTYPE add_MapChanged(C::MapChangedEventHandler<HSTRING, HSTRING>*, EventRegistrationToken* t) override { t->value = next_++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_MapChanged(EventRegistrationToken) override { return S_OK; }
};

// ---- generated stand-ins (WineCompatGen.inc, made from the SDK headers by tools/winrtgen) ------------
// StoreContext: offline the app is fully licensed and nothing else is offered. Launcher: nothing is launched.
void LogText(const char* what, HSTRING s)
{ char l[600]; sprintf_s(l, "[ForgeShim] compat: %s %ls\n", what, s ? WindowsGetStringRawBuffer(s, nullptr) : L""); OutputDebugStringA(l); }
void LogUri(const char* what, ABI::Windows::Foundation::IUriRuntimeClass* uri)
{
    HSTRING s = nullptr; if (uri) uri->get_AbsoluteUri(&s);
    char l[600]; sprintf_s(l, "[ForgeShim] compat: %s %ls (not opened offline)\n", what, s ? WindowsGetStringRawBuffer(s, nullptr) : L"?"); OutputDebugStringA(l);
    if (s) WindowsDeleteString(s);
}
#pragma warning(push)
#pragma warning(disable: 4100)
#include "WineCompatGen.inc"
#pragma warning(pop)

// ---- Windows.UI.Popups.MessageDialog: logged (h5campaign\\logs\\game.log), default command taken ------------------------
namespace P = ABI::Windows::UI::Popups;
class Command final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, P::IUICommand> {
    InspectableClass(L"Windows.UI.Popups.UICommand", BaseTrust)
public:
    HSTRING label = nullptr; ComPtr<P::IUICommandInvokedHandler> invoked; ComPtr<IInspectable> id;
    ~Command() { if (label) WindowsDeleteString(label); }
    HRESULT STDMETHODCALLTYPE get_Label(HSTRING* v) override { return WindowsDuplicateString(label, v); }
    HRESULT STDMETHODCALLTYPE put_Label(HSTRING v) override { if (label) WindowsDeleteString(label); return WindowsDuplicateString(v, &label); }
    HRESULT STDMETHODCALLTYPE get_Invoked(P::IUICommandInvokedHandler** v) override { return invoked.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE put_Invoked(P::IUICommandInvokedHandler* v) override { invoked = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Id(IInspectable** v) override { return id.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE put_Id(IInspectable* v) override { id = v; return S_OK; }
};
class CommandFactory final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, P::IUICommandFactory> {
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { return Make<Command>().CopyTo(out); }
    HRESULT STDMETHODCALLTYPE Create(HSTRING label, P::IUICommand** out) override { auto c = Make<Command>(); c->put_Label(label); return c.CopyTo(out); }
    HRESULT STDMETHODCALLTYPE CreateWithHandler(HSTRING label, P::IUICommandInvokedHandler* h, P::IUICommand** out) override { auto c = Make<Command>(); c->put_Label(label); c->invoked = h; return c.CopyTo(out); }
    HRESULT STDMETHODCALLTYPE CreateWithHandlerAndId(HSTRING label, P::IUICommandInvokedHandler* h, IInspectable* id, P::IUICommand** out) override { auto c = Make<Command>(); c->put_Label(label); c->invoked = h; c->id = id; return c.CopyTo(out); }
};
class Dialog final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, P::IMessageDialog> {
    InspectableClass(L"Windows.UI.Popups.MessageDialog", BaseTrust)
    std::wstring title_, content_; ComPtr<Vector<P::IUICommand*>> commands_ = Make<Vector<P::IUICommand*>>(); UINT32 def_ = 0, cancel_ = 0; P::MessageDialogOptions options_ = P::MessageDialogOptions_None;
public:
    Dialog(HSTRING content, HSTRING title) { if (content) content_ = WindowsGetStringRawBuffer(content, nullptr); if (title) title_ = WindowsGetStringRawBuffer(title, nullptr); }
    HRESULT STDMETHODCALLTYPE get_Title(HSTRING* v) override { return Str(title_.c_str(), v); }
    HRESULT STDMETHODCALLTYPE put_Title(HSTRING v) override { title_ = v ? WindowsGetStringRawBuffer(v, nullptr) : L""; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Commands(ABI::Windows::Foundation::Collections::IVector<P::IUICommand*>** v) override { return commands_.CopyTo(v); }
    HRESULT STDMETHODCALLTYPE get_DefaultCommandIndex(UINT32* v) override { *v = def_; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_DefaultCommandIndex(UINT32 v) override { def_ = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CancelCommandIndex(UINT32* v) override { *v = cancel_; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_CancelCommandIndex(UINT32 v) override { cancel_ = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Content(HSTRING* v) override { return Str(content_.c_str(), v); }
    HRESULT STDMETHODCALLTYPE put_Content(HSTRING v) override { content_ = v ? WindowsGetStringRawBuffer(v, nullptr) : L""; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Options(P::MessageDialogOptions* v) override { *v = options_; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Options(P::MessageDialogOptions v) override { options_ = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE ShowAsync(IAsyncOperation<P::IUICommand*>** op) override
    {
        char l[2200]; sprintf_s(l, "[ForgeShim] compat: MessageDialog \"%ls\": %ls\n", title_.c_str(), content_.c_str()); OutputDebugStringA(l);
        // Not shown: on the PS5 there is nothing to click it with. The default command is taken.
        ComPtr<P::IUICommand> chosen; if (def_ < commands_->items.size()) chosen = commands_->items[def_];
        if (chosen) { ComPtr<P::IUICommandInvokedHandler> h; chosen->get_Invoked(&h); if (h) h->Invoke(chosen.Get()); }
        return Make<Done<P::IUICommand*, P::IUICommand>>(chosen.Get()).CopyTo(op);
    }
};
class DialogFactory final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, P::IMessageDialogFactory> {
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { if (out) *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Create(HSTRING content, P::IMessageDialog** out) override { return Make<Dialog>(content, nullptr).CopyTo(out); }
    HRESULT STDMETHODCALLTYPE CreateWithTitle(HSTRING content, HSTRING title, P::IMessageDialog** out) override { return Make<Dialog>(content, title).CopyTo(out); }
};

ComPtr<ABI::Windows::System::IUser> TheUser() { static ComPtr<ABI::Windows::System::IUser> user = [] { ComPtr<ABI::Windows::System::IUser> u; Make<OfflineUser>().As(&u); return u; }(); return user; }

// ---- Windows.System.Threading.ThreadPoolTimer: Win32 thread-pool timers -----------------------------
namespace T = ABI::Windows::System::Threading;
class PoolTimer final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, T::IThreadPoolTimer> {
    InspectableClass(L"Windows.System.Threading.ThreadPoolTimer", BaseTrust)
    PTP_TIMER timer_ = nullptr; ComPtr<T::ITimerElapsedHandler> elapsed_; ComPtr<T::ITimerDestroyedHandler> destroyed_;
    TimeSpan delay_{}, period_{}; std::atomic<bool> armed_{false}, done_{false};
    static void CALLBACK Fire(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER)
    {
        auto self = static_cast<PoolTimer*>(context); if (self->done_) return;
        if (self->elapsed_) self->elapsed_->Invoke(self);
        if (!self->period_.Duration) self->Finish();   // one-shot: finished after its callback
    }
    void Finish()
    {
        if (done_.exchange(true)) return;
        if (destroyed_) destroyed_->Invoke(this);
        if (armed_.exchange(false)) Release();   // the reference held while armed
    }
public:
    PoolTimer(T::ITimerElapsedHandler* e, T::ITimerDestroyedHandler* d, TimeSpan delay, TimeSpan period) : elapsed_(e), destroyed_(d), delay_(delay), period_(period) {}
    ~PoolTimer() { if (timer_) { SetThreadpoolTimer(timer_, nullptr, 0, 0); CloseThreadpoolTimer(timer_); } }
    HRESULT Start()
    {
        timer_ = CreateThreadpoolTimer(&Fire, this, nullptr); if (!timer_) return HRESULT_FROM_WIN32(GetLastError());
        LONGLONG due = -(period_.Duration && !delay_.Duration ? period_.Duration : delay_.Duration); if (due == 0) due = -1;
        FILETIME ft; ft.dwLowDateTime = static_cast<DWORD>(due); ft.dwHighDateTime = static_cast<DWORD>(due >> 32);
        AddRef(); armed_ = true;
        SetThreadpoolTimer(timer_, &ft, static_cast<DWORD>(period_.Duration / 10000), 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Period(TimeSpan* v) override { *v = period_; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Delay(TimeSpan* v) override { *v = delay_; return S_OK; }
    HRESULT STDMETHODCALLTYPE Cancel() override { if (timer_) SetThreadpoolTimer(timer_, nullptr, 0, 0); Finish(); return S_OK; }
};
class PoolTimerStatics final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, T::IThreadPoolTimerStatics> {
    static HRESULT Create(T::ITimerElapsedHandler* e, T::ITimerDestroyedHandler* d, TimeSpan delay, TimeSpan period, T::IThreadPoolTimer** out)
    {
        if (!out) return E_POINTER; auto t = Make<PoolTimer>(e, d, delay, period); HRESULT hr = t->Start(); if (FAILED(hr)) return hr; return t.CopyTo(out);
    }
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { if (out) *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreatePeriodicTimer(T::ITimerElapsedHandler* h, TimeSpan period, T::IThreadPoolTimer** t) override { return Create(h, nullptr, {}, period, t); }
    HRESULT STDMETHODCALLTYPE CreateTimer(T::ITimerElapsedHandler* h, TimeSpan delay, T::IThreadPoolTimer** t) override { return Create(h, nullptr, delay, {}, t); }
    HRESULT STDMETHODCALLTYPE CreatePeriodicTimerWithCompletion(T::ITimerElapsedHandler* h, TimeSpan period, T::ITimerDestroyedHandler* d, T::IThreadPoolTimer** t) override { return Create(h, d, {}, period, t); }
    HRESULT STDMETHODCALLTYPE CreateTimerWithCompletion(T::ITimerElapsedHandler* h, TimeSpan delay, T::ITimerDestroyedHandler* d, T::IThreadPoolTimer** t) override { return Create(h, d, delay, {}, t); }
};

// ---- Windows.UI.Core.CoreCursor ---------------------------------------------------------------------
namespace U = ABI::Windows::UI::Core;
class Cursor final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, U::ICoreCursor> {
    InspectableClass(L"Windows.UI.Core.CoreCursor", BaseTrust)
    U::CoreCursorType type_; UINT32 id_;
public:
    Cursor(U::CoreCursorType t, UINT32 id) : type_(t), id_(id) {}
    HRESULT STDMETHODCALLTYPE get_Id(UINT32* v) override { *v = id_; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Type(U::CoreCursorType* v) override { *v = type_; return S_OK; }
};
class CursorFactory final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, U::ICoreCursorFactory> {
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { if (out) *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateCursor(U::CoreCursorType t, UINT32 id, U::ICoreCursor** out) override { if (!out) return E_POINTER; return Make<Cursor>(t, id).CopyTo(out); }
};

// ---- Windows.ApplicationModel.ExtendedExecution.ExtendedExecutionSession -------------------------
// A desktop game is never suspended; the request is simply allowed.
namespace X = ABI::Windows::ApplicationModel::ExtendedExecution;
class ExtendedSession final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, X::IExtendedExecutionSession, IClosable> {
    InspectableClass(L"Windows.ApplicationModel.ExtendedExecution.ExtendedExecutionSession", BaseTrust)
    X::ExtendedExecutionReason reason_ = X::ExtendedExecutionReason_Unspecified; HSTRING desc_ = nullptr; UINT32 progress_ = 0; INT64 next_ = 1;
public:
    ~ExtendedSession() { if (desc_) WindowsDeleteString(desc_); }
    HRESULT STDMETHODCALLTYPE get_Reason(X::ExtendedExecutionReason* v) override { *v = reason_; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Reason(X::ExtendedExecutionReason v) override { reason_ = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Description(HSTRING* v) override { return WindowsDuplicateString(desc_, v); }
    HRESULT STDMETHODCALLTYPE put_Description(HSTRING v) override { if (desc_) WindowsDeleteString(desc_); return WindowsDuplicateString(v, &desc_); }
    HRESULT STDMETHODCALLTYPE get_PercentProgress(UINT32* v) override { *v = progress_; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_PercentProgress(UINT32 v) override { progress_ = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE add_Revoked(ITypedEventHandler<IInspectable*, X::ExtendedExecutionRevokedEventArgs*>*, EventRegistrationToken* t) override { t->value = next_++; return S_OK; }
    HRESULT STDMETHODCALLTYPE remove_Revoked(EventRegistrationToken) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RequestExtensionAsync(IAsyncOperation<X::ExtendedExecutionResult>** op) override
    { if (!op) return E_POINTER; return Make<DoneValue<X::ExtendedExecutionResult>>(X::ExtendedExecutionResult_Allowed).CopyTo(op); }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};

// ---- Windows.Globalization.Language -----------------------------------------------------------------
namespace G = ABI::Windows::Globalization;
class Language final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, G::ILanguage> {
    InspectableClass(L"Windows.Globalization.Language", BaseTrust)
    wchar_t tag_[LOCALE_NAME_MAX_LENGTH]{};
public:
    explicit Language(const wchar_t* tag) { wcsncpy_s(tag_, tag && *tag ? tag : L"en-US", _TRUNCATE); }
    HRESULT STDMETHODCALLTYPE get_LanguageTag(HSTRING* v) override { return Str(tag_, v); }
    HRESULT STDMETHODCALLTYPE get_DisplayName(HSTRING* v) override { wchar_t n[128]; return GetLocaleInfoEx(tag_, LOCALE_SLOCALIZEDDISPLAYNAME, n, 128) ? Str(n, v) : Str(tag_, v); }
    HRESULT STDMETHODCALLTYPE get_NativeName(HSTRING* v) override { wchar_t n[128]; return GetLocaleInfoEx(tag_, LOCALE_SNATIVEDISPLAYNAME, n, 128) ? Str(n, v) : Str(tag_, v); }
    HRESULT STDMETHODCALLTYPE get_Script(HSTRING* v) override { wchar_t n[16]; return GetLocaleInfoEx(tag_, LOCALE_SSCRIPTS, n, 16) && n[0] ? (n[4] = 0, Str(n, v)) : Str(L"Latn", v); }
};
class LanguageFactory final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, IActivationFactory, G::ILanguageFactory, G::ILanguageStatics> {
public:
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** out) override { if (out) *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateLanguage(HSTRING tag, G::ILanguage** out) override
    { if (!out) return E_POINTER; return Make<Language>(WindowsGetStringRawBuffer(tag, nullptr)).CopyTo(out); }
    HRESULT STDMETHODCALLTYPE IsWellFormed(HSTRING tag, boolean* ok) override
    { if (!ok) return E_POINTER; *ok = WindowsGetStringLen(tag) > 0 && WindowsGetStringLen(tag) < LOCALE_NAME_MAX_LENGTH; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CurrentInputMethodLanguageTag(HSTRING* v) override
    { wchar_t n[LOCALE_NAME_MAX_LENGTH]; return GetUserDefaultLocaleName(n, LOCALE_NAME_MAX_LENGTH) ? Str(n, v) : Str(L"en-US", v); }
};
}

// Used by the shim's own LocalSettings when the system has no PropertySet.
HRESULT CompatPropertySet(C::IPropertySet** out) { if (!out) return E_POINTER; return Make<PropertySet>().CopyTo(out); }
// Used by the shim's offline sign-in (WebAuth.cpp) when the system has no StringMap.
HRESULT CompatStringMap(C::IMap<HSTRING, HSTRING>** out) { if (!out) return E_POINTER; return Make<StringMap>().CopyTo(out); }

// Classes that are only used when the system has none.
// replace = true: used under Wine even when Wine has the class (Wine's is known to be incomplete there).
struct Fallback { const wchar_t* name; HRESULT (*make)(REFIID, void**); bool replace = false; };
bool OnWine() { static const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr; return wine; }
template <typename F> HRESULT MakeFactory(REFIID iid, void** out) { return Make<F>().CopyTo(iid, out); }
const Fallback g_fallbacks[] = {
    { L"Windows.Devices.Enumeration.DeviceInformation", MakeFactory<DevInfoStatics>, true },   // no devices are listed
    { L"Windows.ApplicationModel.ExtendedExecution.ExtendedExecutionSession", MakeFactory<Activatable<ExtendedSession>> },
    // Wine's PropertySet::Insert AddRefs the value without a null check; the game stores null values (MultiplayerMapEngine).
    { L"Windows.Foundation.Collections.PropertySet", MakeFactory<Activatable<PropertySet>>, true },
    { L"Windows.Foundation.Collections.StringMap", MakeFactory<Activatable<StringMap>> },
    { L"Windows.Services.Store.StoreContext", MakeFactory<StoreCtxStatics> },
    { L"Windows.System.Launcher", MakeFactory<LauncherStatics> },
    { L"Windows.System.LauncherOptions", MakeFactory<Activatable<LauncherOptions>> },
    { L"Windows.UI.Core.CoreCursor", MakeFactory<CursorFactory> },
    { L"Windows.UI.Popups.MessageDialog", MakeFactory<DialogFactory> },
    { L"Windows.System.Threading.ThreadPoolTimer", MakeFactory<PoolTimerStatics> },
    { L"Windows.Networking.Connectivity.NetworkInformation", MakeFactory<NetInfoStatics>, true },   // a local network with "internet access"
    { L"Windows.UI.ViewManagement.UISettings", MakeFactory<Activatable<UiSettings>>, true },   // Wine's stubs DoubleClickTime
    { L"Windows.Networking.PushNotifications.PushNotificationChannelManager", MakeFactory<PushStatics> },   // offline: no channel
    { L"Windows.System.UserPicker", MakeFactory<UserPickerStatics> },
    { L"Windows.System.User", MakeFactory<UserStatics> },   // one local user
    { L"Windows.Foundation.Diagnostics.LoggingChannel", MakeFactory<LogChannelFactory> },        // telemetry: dropped
    { L"Windows.Foundation.Diagnostics.LoggingChannelOptions", MakeFactory<LogChannelOptionsFactory> },
    { L"Windows.Foundation.Diagnostics.LoggingOptions", MakeFactory<LogOptionsFactory> },
    { L"Windows.Foundation.Diagnostics.LoggingFields", MakeFactory<LogFieldsFactory> },
    { L"Windows.UI.Popups.UICommand", MakeFactory<CommandFactory> },
};

// Returns true when name is one of the classes this file covers (out/hr then hold the result).
bool CompatFactory(HSTRING name, REFIID iid, void** out, RealFactory real, HRESULT& hr)
{
    const wchar_t* raw = WindowsGetStringRawBuffer(name, nullptr);
    if (!wcscmp(raw, L"Windows.Globalization.Language"))
    {
        if (RealHas(real, name, __uuidof(G::ILanguageStatics), iid, out)) { hr = S_OK; return true; }
        static bool said = false; if (!said) { said = true; Note("Windows.Globalization.Language (system class lacks ILanguageStatics)"); }
        hr = Make<LanguageFactory>().CopyTo(iid, out); return true;
    }
    if (!wcscmp(raw, L"Windows.Devices.Geolocation.Geolocator"))   // Wine's has no IGeolocatorStatics
    {
        if (RealHas(real, name, __uuidof(GL::IGeolocatorStatics), iid, out)) { hr = S_OK; return true; }
        static bool said = false; if (!said) { said = true; Note("Windows.Devices.Geolocation.Geolocator (system class lacks IGeolocatorStatics; location is not available)"); }
        hr = Make<LocatorStatics>().CopyTo(iid, out); return true;
    }
    for (auto& f : g_fallbacks)
    {
        if (wcscmp(raw, f.name)) continue;
        if (!(f.replace && OnWine())) { hr = real(name, iid, out); if (SUCCEEDED(hr)) return true; }
        char l[200]; sprintf_s(l, "%ls (%s)", f.name, f.replace && OnWine() ? "Wine's is incomplete" : "not in the system"); Note(l);
        hr = f.make(iid, out); return true;
    }
    return false;
}

// Win32 functions Wine has only as stubs, replaced in the game's imports (under Wine only).
static HRESULT WINAPI CompatCopyFile2(PCWSTR from, PCWSTR to, COPYFILE2_EXTENDED_PARAMETERS* p)
{
    BOOL failIfExists = p && (p->dwCopyFlags & COPY_FILE_FAIL_IF_EXISTS);
    return CopyFileW(from, to, failIfExists) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}
// Offline the game still needs its network layer (local "transport") to start: Forge only starts it when the Xbox
// Live wrapper says there is an internet connection, and then shows "NOT ONLINE" at the title. Answered yes, always.
// ---- Text Services Framework ------------------------------------------------------------------------
// The game's text input (the in-game keyboard and text boxes) asks TF_ThreadMgr for ITfThreadMgr2, a Windows 8
// interface Wine's msctf doesn't have; without it the input system's text store is never made and the game crashes
// on its next window resize. ITfThreadMgr2 is ITfThreadMgrEx minus AssociateFocus plus two keystroke switches, so
// Wine's ITfThreadMgrEx stands in for it.
namespace {
const CLSID kThreadMgr = { 0x529a9e6b, 0x6587, 0x4f23, { 0xab, 0x9e, 0x9c, 0x7d, 0x68, 0x3e, 0x3c, 0x50 } };
struct ThreadMgr2 final : ITfThreadMgr2
{
    std::atomic<ULONG> refs{ 1 };
    ComPtr<ITfThreadMgrEx> ex;
    explicit ThreadMgr2(ITfThreadMgrEx* e) : ex(e) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override
    {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(ITfThreadMgr2)) { *out = static_cast<ITfThreadMgr2*>(this); AddRef(); return S_OK; }
        return ex->QueryInterface(iid, out);
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    STDMETHODIMP_(ULONG) Release() override { ULONG r = --refs; if (!r) delete this; return r; }
    STDMETHODIMP Activate(TfClientId* id) override { return ex->Activate(id); }
    STDMETHODIMP Deactivate() override { return ex->Deactivate(); }
    STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr** d) override { return ex->CreateDocumentMgr(d); }
    STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs** e) override { return ex->EnumDocumentMgrs(e); }
    STDMETHODIMP GetFocus(ITfDocumentMgr** d) override { return ex->GetFocus(d); }
    STDMETHODIMP SetFocus(ITfDocumentMgr* d) override { return ex->SetFocus(d); }
    STDMETHODIMP IsThreadFocus(BOOL* f) override { return ex->IsThreadFocus(f); }
    STDMETHODIMP GetFunctionProvider(REFCLSID c, ITfFunctionProvider** p) override { return ex->GetFunctionProvider(c, p); }
    STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders** e) override { return ex->EnumFunctionProviders(e); }
    STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr** c) override { return ex->GetGlobalCompartment(c); }
    STDMETHODIMP ActivateEx(TfClientId* id, DWORD flags) override { return ex->ActivateEx(id, flags); }
    STDMETHODIMP GetActiveFlags(DWORD* f) override { return ex->GetActiveFlags(f); }
    STDMETHODIMP SuspendKeystrokeHandling() override { return S_OK; }
    STDMETHODIMP ResumeKeystrokeHandling() override { return S_OK; }
};
using CoCreateFromApp = HRESULT(WINAPI*)(REFCLSID, IUnknown*, DWORD, void*, DWORD, MULTI_QI*);
CoCreateFromApp RealCoCreateFromApp()
{
    static CoCreateFromApp f = reinterpret_cast<CoCreateFromApp>(GetProcAddress(LoadLibraryW(L"combase.dll"), "CoCreateInstanceFromApp"));
    return f;
}
HRESULT WINAPI CompatCoCreateInstanceFromApp(REFCLSID clsid, IUnknown* outer, DWORD ctx, void* reserved, DWORD count, MULTI_QI* results)
{
    CoCreateFromApp real = RealCoCreateFromApp();
    if (!real) return E_NOTIMPL;
    HRESULT hr = real(clsid, outer, ctx, reserved, count, results);
    if (clsid != kThreadMgr || outer || !results) return hr;
    bool fixed = false, missing = false;
    for (DWORD i = 0; i < count; ++i)
    {
        if (!results[i].pIID || *results[i].pIID != __uuidof(ITfThreadMgr2) || SUCCEEDED(results[i].hr)) { if (FAILED(results[i].hr)) missing = true; continue; }
        MULTI_QI q{ &__uuidof(ITfThreadMgrEx), nullptr, S_OK };
        if (FAILED(real(clsid, nullptr, ctx, reserved, 1, &q)) || FAILED(q.hr) || !q.pItf) { missing = true; continue; }
        results[i].pItf = static_cast<ITfThreadMgr2*>(new ThreadMgr2(static_cast<ITfThreadMgrEx*>(q.pItf)));
        q.pItf->Release();
        results[i].hr = S_OK; fixed = true;
    }
    if (!fixed) return hr;
    static std::atomic<bool> said{ false };
    if (!said.exchange(true)) Note("TF_ThreadMgr has no ITfThreadMgr2 (wrapping ITfThreadMgrEx)");
    return missing ? CO_S_NOTALLINTERFACES : S_OK;
}
}

static bool AlwaysConnected(void*) { return true; }
void* WineApiReplacement(const char* name)
{
    if (strstr(name, "hasInternetConnection@XboxLiveServicesEngineInterface")) return reinterpret_cast<void*>(&AlwaysConnected);
    if (!OnWine()) return nullptr;
    if (!strcmp(name, "CopyFile2")) return reinterpret_cast<void*>(&CompatCopyFile2);
    if (!strcmp(name, "CoCreateInstanceFromApp")) return reinterpret_cast<void*>(&CompatCoCreateInstanceFromApp);
    return nullptr;
}
