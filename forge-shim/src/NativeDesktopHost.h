#pragma once
#include <windows.h>
#include <windows.applicationmodel.core.h>
#include <windows.ui.core.h>
namespace h5::desktop {
HRESULT Run(ABI::Windows::ApplicationModel::Core::IFrameworkViewSource*) noexcept;
HRESULT GetCurrentView(ABI::Windows::ApplicationModel::Core::ICoreApplicationView**) noexcept;
HRESULT GetCurrentWindow(ABI::Windows::UI::Core::ICoreWindow**) noexcept;
HRESULT GetCoreWindowStaticFactory(REFIID,void**) noexcept;
}
