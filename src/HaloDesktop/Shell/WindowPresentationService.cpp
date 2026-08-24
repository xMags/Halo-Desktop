#include "pch.h"
#include "Shell/WindowPresentationService.h"

#include <stdexcept>

namespace
{
    LONG_PTR ReadWindowStyle(HWND window, int index)
    {
        SetLastError(ERROR_SUCCESS);
        auto const value = GetWindowLongPtrW(window, index);
        if (value == 0)
        {
            auto const error = GetLastError();
            if (error != ERROR_SUCCESS)
            {
                winrt::throw_hresult(HRESULT_FROM_WIN32(error));
            }
        }
        return value;
    }

    void WriteWindowStyle(HWND window, int index, LONG_PTR value)
    {
        SetLastError(ERROR_SUCCESS);
        auto const previous = SetWindowLongPtrW(window, index, value);
        if (previous == 0)
        {
            auto const error = GetLastError();
            if (error != ERROR_SUCCESS)
            {
                winrt::throw_hresult(HRESULT_FROM_WIN32(error));
            }
        }
    }

    MONITORINFO MonitorInfoForWindow(HWND window)
    {
        auto const monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        if (!monitor)
        {
            winrt::throw_last_error();
        }

        MONITORINFO info{ sizeof(MONITORINFO) };
        winrt::check_bool(GetMonitorInfoW(monitor, &info));
        return info;
    }

    void SizeWindowToMonitor(HWND window, HWND insertAfter)
    {
        auto const monitor = MonitorInfoForWindow(window);
        auto const& bounds = monitor.rcMonitor;
        winrt::check_bool(SetWindowPos(window, insertAfter, bounds.left, bounds.top, bounds.right - bounds.left,
                                       bounds.bottom - bounds.top,
                                       SWP_SHOWWINDOW | SWP_NOOWNERZORDER | SWP_FRAMECHANGED));
    }

    void RestoreWindow(HWND window, LONG_PTR style, LONG_PTR extendedStyle, WINDOWPLACEMENT const& placement,
                       RECT const& bounds, bool wasMaximized)
    {
        WriteWindowStyle(window, GWL_STYLE, style);
        WriteWindowStyle(window, GWL_EXSTYLE, extendedStyle);
        auto const insertAfter = (extendedStyle & static_cast<LONG_PTR>(WS_EX_TOPMOST)) != 0
            ? HWND_TOPMOST
            : HWND_NOTOPMOST;
        if (wasMaximized)
        {
            winrt::check_bool(SetWindowPlacement(window, &placement));
            winrt::check_bool(SetWindowPos(window, insertAfter, 0, 0, 0, 0,
                                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER |
                                               SWP_NOACTIVATE | SWP_FRAMECHANGED));
            return;
        }

        winrt::check_bool(SetWindowPos(window, insertAfter, bounds.left, bounds.top, bounds.right - bounds.left,
                                       bounds.bottom - bounds.top,
                                       SWP_SHOWWINDOW | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED));
    }
} // namespace

namespace HaloDesktop::Shell
{
    void WindowPresentationService::Attach(std::uintptr_t windowHandle,
                                           winrt::Microsoft::UI::Xaml::Controls::RowDefinition const& titleBarRow)
    {
        if (windowHandle == 0 || !titleBarRow)
        {
            throw std::invalid_argument("Window presentation requires a window handle and title row");
        }
        m_windowHandle = windowHandle;
        m_titleBarRow = titleBarRow;
    }
    void WindowPresentationService::Detach() noexcept
    {
        m_windowHandle = 0;
        m_titleBarRow = nullptr;
        m_windowedPlacement = { sizeof(WINDOWPLACEMENT) };
        m_windowedBounds = {};
        m_windowedStyle = 0;
        m_windowedExtendedStyle = 0;
        m_wasMaximized = false;
        m_fullscreen = false;
        m_windowActive = true;
    }
    void WindowPresentationService::SetFullscreen(bool fullscreen)
    {
        if (m_windowHandle == 0 || !m_titleBarRow)
        {
            throw std::logic_error("Window presentation is not attached");
        }
        if (m_fullscreen == fullscreen)
        {
            if (fullscreen)
            {
                SizeWindowToMonitor(
                    reinterpret_cast<HWND>(m_windowHandle),
                    m_windowActive ? HWND_TOPMOST : HWND_NOTOPMOST);
            }
            return;
        }

        auto const window = reinterpret_cast<HWND>(m_windowHandle);
        if (fullscreen)
        {
            WINDOWPLACEMENT placement{ sizeof(WINDOWPLACEMENT) };
            winrt::check_bool(GetWindowPlacement(window, &placement));
            RECT windowedBounds{};
            winrt::check_bool(GetWindowRect(window, &windowedBounds));

            auto const windowedStyle = ReadWindowStyle(window, GWL_STYLE);
            auto const windowedExtendedStyle = ReadWindowStyle(window, GWL_EXSTYLE);
            auto const wasMaximized = IsZoomed(window) != FALSE;
            auto const fullscreenStyle =
                (windowedStyle & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) | static_cast<LONG_PTR>(WS_POPUP);
            auto const fullscreenExtendedStyle =
                windowedExtendedStyle & ~static_cast<LONG_PTR>(WS_EX_WINDOWEDGE);

            // The taskbar is itself topmost, so monitor-sized popup geometry alone can
            // leave it over the transport controls. Promote Halo while it is active;
            // SetWindowActive demotes it as soon as the user switches to another app.
            WriteWindowStyle(window, GWL_STYLE, fullscreenStyle);
            try
            {
                WriteWindowStyle(window, GWL_EXSTYLE, fullscreenExtendedStyle);
                SizeWindowToMonitor(window, m_windowActive ? HWND_TOPMOST : HWND_NOTOPMOST);
            }
            catch (...)
            {
                try
                {
                    RestoreWindow(window, windowedStyle, windowedExtendedStyle, placement, windowedBounds,
                                  wasMaximized);
                }
                catch (...)
                {
                }
                throw;
            }

            m_windowedPlacement = placement;
            m_windowedBounds = windowedBounds;
            m_windowedStyle = windowedStyle;
            m_windowedExtendedStyle = windowedExtendedStyle;
            m_wasMaximized = wasMaximized;
            m_titleBarRow.Height({ 0.0, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
            m_fullscreen = true;
            return;
        }

        RestoreWindow(window, m_windowedStyle, m_windowedExtendedStyle, m_windowedPlacement, m_windowedBounds,
                      m_wasMaximized);
        m_titleBarRow.Height({ 32.0, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
        m_windowedPlacement = { sizeof(WINDOWPLACEMENT) };
        m_windowedBounds = {};
        m_windowedStyle = 0;
        m_windowedExtendedStyle = 0;
        m_wasMaximized = false;
        m_fullscreen = false;
    }
    void WindowPresentationService::SetWindowActive(bool active) noexcept
    {
        m_windowActive = active;
        if (!m_fullscreen || m_windowHandle == 0)
        {
            return;
        }
        auto const window = reinterpret_cast<HWND>(m_windowHandle);
        static_cast<void>(SetWindowPos(
            window,
            active ? HWND_TOPMOST : HWND_NOTOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER));
    }
    bool WindowPresentationService::IsFullscreen() const noexcept
    {
        return m_fullscreen;
    }

    std::uintptr_t WindowPresentationService::WindowHandle() const
    {
        if (m_windowHandle == 0)
        {
            throw std::logic_error("Window presentation is not attached");
        }
        return m_windowHandle;
    }
} // namespace HaloDesktop::Shell
