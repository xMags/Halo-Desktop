#include "pch.h"
#include "Shell/WindowSizing.h"

#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Windows.Graphics.h>

namespace
{
    winrt::Windows::Graphics::SizeInt32 WindowSizeForClientDips(HWND windowHandle, std::int32_t width,
                                                                std::int32_t height)
    {
        auto const dpi = GetDpiForWindow(windowHandle);
        RECT bounds{
            0,
            0,
            MulDiv(width, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
            MulDiv(height, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
        };

        auto const style = static_cast<DWORD>(GetWindowLongPtrW(windowHandle, GWL_STYLE));
        auto const extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(windowHandle, GWL_EXSTYLE));
        winrt::check_bool(AdjustWindowRectExForDpi(&bounds, style, FALSE, extendedStyle, dpi));

        return {
            bounds.right - bounds.left,
            bounds.bottom - bounds.top,
        };
    }

    winrt::Windows::Foundation::IReference<std::int32_t> BoxDimension(std::int32_t value)
    {
        return winrt::box_value(value).as<winrt::Windows::Foundation::IReference<std::int32_t>>();
    }
} // namespace

namespace HaloDesktop::Shell
{
    WindowSizing::WindowSizing(winrt::Microsoft::UI::Xaml::Window const& window)
    {
        HWND windowHandle{};
        winrt::check_hresult(window.as<::IWindowNative>()->get_WindowHandle(&windowHandle));
        // HWND is an opaque pointer supplied by WinUI. uintptr_t keeps it out
        // of the platform-neutral service interface without changing bits.
        m_windowHandle = reinterpret_cast<std::uintptr_t>(windowHandle);

        auto const windowId = winrt::Microsoft::UI::GetWindowIdFromWindow(windowHandle);
        m_appWindow = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);

        auto const defaultSize = WindowSizeForClientDips(windowHandle, 1280, 800);
        auto const minimumSize = WindowSizeForClientDips(windowHandle, 960, 640);
        m_appWindow.Resize(defaultSize);

        auto const presenter = m_appWindow.Presenter().as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>();
        presenter.PreferredMinimumWidth(BoxDimension(minimumSize.Width));
        presenter.PreferredMinimumHeight(BoxDimension(minimumSize.Height));
    }

    winrt::Microsoft::UI::Windowing::AppWindow const& WindowSizing::AppWindow() const noexcept
    {
        return m_appWindow;
    }

    std::uintptr_t WindowSizing::WindowHandle() const noexcept
    {
        return m_windowHandle;
    }
} // namespace HaloDesktop::Shell
