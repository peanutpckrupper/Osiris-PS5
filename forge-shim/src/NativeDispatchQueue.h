#pragma once
#include <windows.h>
#include <windows.ui.core.h>
#include <winrt/Windows.Foundation.h>
#include <wrl/client.h>
#include <algorithm>
#include <coroutine>
#include <cstdio>
#include <memory>
#include <mutex>
#include <vector>

namespace h5::desktop
{
inline constexpr UINT NativeDispatchMessage = WM_APP + 0x515;

class NativeDispatchState
{
    struct Pending
    {
        int priority;
        std::coroutine_handle<> continuation;
        HRESULT* result;
    };
    std::mutex mutex_;
    std::vector<Pending> pending_;
    HWND window_{};
    DWORD owner_{};
    bool closed_{};
public:
    explicit NativeDispatchState(HWND window) : window_(window),
        owner_(GetWindowThreadProcessId(window, nullptr)) {}
    DWORD OwnerThread() const noexcept { return owner_; }
    bool IsOpen()
    {
        std::lock_guard lock(mutex_);
        return !closed_ && owner_ && IsWindow(window_);
    }
    bool Suspend(std::coroutine_handle<> continuation, HRESULT* result, int priority)
    {
        std::lock_guard lock(mutex_);
        if (closed_ || !owner_ || !IsWindow(window_))
        {
            *result = RO_E_CLOSED;
            return false;
        }
        *result = S_OK;
        pending_.push_back({priority, continuation, result});
        // Hold the lock until posting succeeds or the entry is removed. The UI
        // may resume and destroy the coroutine immediately after we unlock.
        if (!PostMessageW(window_, NativeDispatchMessage, 0, 0))
        {
            DWORD error = GetLastError();
            pending_.pop_back();
            *result = HRESULT_FROM_WIN32(error ? error : ERROR_INVALID_WINDOW_HANDLE);
            return false;
        }
        return true;
    }
    HRESULT ProcessPending()
    {
        if (GetCurrentThreadId() != owner_) return RPC_E_WRONG_THREAD;
        // Bound each batch so queued callbacks cannot monopolize the UI pump.
        for (unsigned count = 0; count < 32; ++count)
        {
            Pending work{};
            {
                std::lock_guard lock(mutex_);
                if (closed_ || pending_.empty()) return S_OK;
                auto next = std::max_element(pending_.begin(), pending_.end(),
                    [](const Pending& a, const Pending& b) { return a.priority < b.priority; });
                work = *next;
                pending_.erase(next);
            }
            work.continuation.resume();
        }
        std::lock_guard lock(mutex_);
        if (!closed_ && !pending_.empty()) PostMessageW(window_, NativeDispatchMessage, 0, 0);
        return S_OK;
    }
    HRESULT Shutdown()
    {
        if (GetCurrentThreadId() != owner_) return RPC_E_WRONG_THREAD;
        std::vector<Pending> abandoned;
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
            abandoned.swap(pending_);
        }
        // Finish pending actions as canceled without invoking their delegates.
        for (auto& work : abandoned)
        {
            *work.result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            work.continuation.resume();
        }
        return S_OK;
    }
};

struct NativeDispatchAwaiter
{
    std::shared_ptr<NativeDispatchState> state;
    int priority;
    HRESULT result = S_OK;
    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> continuation)
    {
        auto keepState = state;
        return keepState->Suspend(continuation, &result, priority);
    }
    void await_resume() const { winrt::check_hresult(result); }
};

inline winrt::Windows::Foundation::IAsyncAction DispatchNativeCallback(
    std::shared_ptr<NativeDispatchState> state, int priority,
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Core::IDispatchedHandler> callback)
{
    auto cancellation = co_await winrt::get_cancellation_token();
    co_await NativeDispatchAwaiter{std::move(state), priority};
    if (cancellation()) throw winrt::hresult_canceled();
    char diagnostic[160]{};
    sprintf_s(diagnostic, "[H5S15] Dispatch callback executing tid=%lu\n", GetCurrentThreadId());
    OutputDebugStringA(diagnostic);
    HRESULT hr = callback->Invoke();
    sprintf_s(diagnostic, "[H5S15] Dispatch callback returned hr=0x%08lX\n", static_cast<unsigned long>(hr));
    OutputDebugStringA(diagnostic);
    winrt::check_hresult(hr);
}

inline HRESULT ScheduleNativeCallback(std::shared_ptr<NativeDispatchState> state,
    ABI::Windows::UI::Core::CoreDispatcherPriority priority,
    ABI::Windows::UI::Core::IDispatchedHandler* callback,
    ABI::Windows::Foundation::IAsyncAction** action) noexcept
{
    if (!action) return E_POINTER;
    *action = nullptr;
    if (!callback || static_cast<int>(priority) < -2 || static_cast<int>(priority) > 1)
        return E_INVALIDARG;
    // Idle dispatch requires input-queue-aware scheduling, not just a lower
    // numeric priority. Keep that capability explicit until implemented.
    if (static_cast<int>(priority) == -2) return E_NOTIMPL;
    try
    {
        if (!state || !state->IsOpen()) return RO_E_CLOSED;
        auto operation = DispatchNativeCallback(std::move(state), static_cast<int>(priority),
            Microsoft::WRL::ComPtr<ABI::Windows::UI::Core::IDispatchedHandler>(callback));
        *action = reinterpret_cast<ABI::Windows::Foundation::IAsyncAction*>(winrt::detach_abi(operation));
        return S_OK;
    }
    catch (...) { return winrt::to_hresult(); }
}
}
