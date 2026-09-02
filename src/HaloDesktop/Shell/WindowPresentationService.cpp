#include "pch.h"
#include "Shell/WindowPresentationService.h"

#include "Shell/WindowPresentationPolicy.h"

#include <optional>
#include <shobjidl_core.h>
#include <stdexcept>
#include <utility>
#include <wrl/client.h>

namespace
{
    struct WindowedPresentation final
    {
        WINDOWPLACEMENT Placement{ sizeof(WINDOWPLACEMENT) };
        RECT Bounds{};
        LONG_PTR Style{};
        LONG_PTR ExtendedStyle{};
        winrt::Microsoft::UI::Xaml::GridLength TitleBarHeight{};
        bool WasMaximized{};
    };

    [[nodiscard]] HWND NativeWindow(std::uintptr_t windowHandle) noexcept
    {
        return reinterpret_cast<HWND>(windowHandle);
    }

    [[nodiscard]] std::optional<LONG_PTR> TryReadWindowStyle(HWND window, int index) noexcept
    {
        SetLastError(ERROR_SUCCESS);
        auto const value = GetWindowLongPtrW(window, index);
        if (value == 0 && GetLastError() != ERROR_SUCCESS)
        {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] bool TryWriteWindowStyle(HWND window, int index, LONG_PTR value) noexcept
    {
        SetLastError(ERROR_SUCCESS);
        auto const previous = SetWindowLongPtrW(window, index, value);
        return previous != 0 || GetLastError() == ERROR_SUCCESS;
    }

    [[nodiscard]] std::optional<MONITORINFO> TryMonitorInfoForWindow(HWND window) noexcept
    {
        auto const monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        if (!monitor)
        {
            return std::nullopt;
        }

        MONITORINFO info{ sizeof(MONITORINFO) };
        if (!GetMonitorInfoW(monitor, &info))
        {
            return std::nullopt;
        }
        return info;
    }

    [[nodiscard]] bool TrySizeWindowToMonitor(HWND window, HWND insertAfter) noexcept
    {
        auto const monitor = TryMonitorInfoForWindow(window);
        if (!monitor)
        {
            return false;
        }

        auto const& bounds = monitor->rcMonitor;
        return SetWindowPos(
                   window,
                   insertAfter,
                   bounds.left,
                   bounds.top,
                   bounds.right - bounds.left,
                   bounds.bottom - bounds.top,
                   SWP_SHOWWINDOW | SWP_NOOWNERZORDER | SWP_FRAMECHANGED) != FALSE;
    }

    [[nodiscard]] bool TryMarkTaskbarFullscreen(HWND window, bool fullscreen) noexcept
    {
        Microsoft::WRL::ComPtr<ITaskbarList2> taskbar;
        if (FAILED(CoCreateInstance(
                CLSID_TaskbarList,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&taskbar))) ||
            FAILED(taskbar->HrInit()))
        {
            return false;
        }
        return SUCCEEDED(taskbar->MarkFullscreenWindow(window, fullscreen ? TRUE : FALSE));
    }

    [[nodiscard]] HWND FullscreenInsertAfter(
        HaloDesktop::Shell::FullscreenZOrder zOrder,
        HaloDesktop::Shell::WindowActivation activation) noexcept
    {
        return HaloDesktop::Shell::ResolveFullscreenTopmost(zOrder, activation)
            ? HWND_TOPMOST
            : HWND_NOTOPMOST;
    }

    [[nodiscard]] bool TrySetFullscreenZOrder(
        HWND window,
        HaloDesktop::Shell::FullscreenZOrder zOrder,
        HaloDesktop::Shell::WindowActivation activation) noexcept
    {
        return SetWindowPos(
                   window,
                   FullscreenInsertAfter(zOrder, activation),
                   0,
                   0,
                   0,
                   0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) != FALSE;
    }

    [[nodiscard]] bool TrySetTitleBarHeight(
        winrt::Microsoft::UI::Xaml::Controls::RowDefinition const& titleBarRow,
        winrt::Microsoft::UI::Xaml::GridLength const& height) noexcept
    {
        try
        {
            titleBarRow.Height(height);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    [[nodiscard]] std::optional<WindowedPresentation> TryCaptureWindowedPresentation(
        HWND window,
        winrt::Microsoft::UI::Xaml::Controls::RowDefinition const& titleBarRow) noexcept
    {
        WindowedPresentation presentation;
        if (!GetWindowPlacement(window, &presentation.Placement) ||
            !GetWindowRect(window, &presentation.Bounds))
        {
            return std::nullopt;
        }

        auto const style = TryReadWindowStyle(window, GWL_STYLE);
        auto const extendedStyle = TryReadWindowStyle(window, GWL_EXSTYLE);
        if (!style || !extendedStyle)
        {
            return std::nullopt;
        }

        try
        {
            presentation.TitleBarHeight = titleBarRow.Height();
        }
        catch (...)
        {
            return std::nullopt;
        }

        presentation.Style = *style;
        presentation.ExtendedStyle = *extendedStyle;
        presentation.WasMaximized = IsZoomed(window) != FALSE;
        return presentation;
    }

    [[nodiscard]] bool TryApplyFullscreenWindow(
        HWND window,
        HaloDesktop::Shell::FullscreenWindowPolicy const& policy,
        HaloDesktop::Shell::WindowActivation activation) noexcept
    {
        auto succeeded = TryWriteWindowStyle(window, GWL_STYLE, policy.Style);
        succeeded = TryWriteWindowStyle(window, GWL_EXSTYLE, policy.ExtendedStyle) && succeeded;
        succeeded =
            TrySizeWindowToMonitor(window, FullscreenInsertAfter(policy.ZOrder, activation)) && succeeded;
        return succeeded;
    }

    [[nodiscard]] bool TryRestoreWindow(HWND window, WindowedPresentation const& presentation) noexcept
    {
        auto succeeded = TryWriteWindowStyle(window, GWL_STYLE, presentation.Style);
        succeeded = TryWriteWindowStyle(window, GWL_EXSTYLE, presentation.ExtendedStyle) && succeeded;

        auto const insertAfter =
            (presentation.ExtendedStyle & static_cast<LONG_PTR>(WS_EX_TOPMOST)) != 0
                ? HWND_TOPMOST
                : HWND_NOTOPMOST;
        if (presentation.WasMaximized)
        {
            succeeded = SetWindowPlacement(window, &presentation.Placement) != FALSE && succeeded;
            succeeded = SetWindowPos(
                            window,
                            insertAfter,
                            0,
                            0,
                            0,
                            0,
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER |
                                SWP_NOACTIVATE | SWP_FRAMECHANGED) != FALSE && succeeded;
            return succeeded;
        }

        succeeded = SetWindowPos(
                        window,
                        insertAfter,
                        presentation.Bounds.left,
                        presentation.Bounds.top,
                        presentation.Bounds.right - presentation.Bounds.left,
                        presentation.Bounds.bottom - presentation.Bounds.top,
                        SWP_SHOWWINDOW | SWP_NOOWNERZORDER | SWP_NOACTIVATE |
                            SWP_FRAMECHANGED) != FALSE && succeeded;
        return succeeded;
    }

    [[nodiscard]] bool TryApplyFullscreenPresentation(
        HWND window,
        HaloDesktop::Shell::FullscreenWindowPolicy const& policy,
        winrt::Microsoft::UI::Xaml::Controls::RowDefinition const& titleBarRow,
        HaloDesktop::Shell::WindowActivation activation) noexcept
    {
        auto const windowApplied = TryApplyFullscreenWindow(window, policy, activation);
        auto const titleBarApplied = TrySetTitleBarHeight(
            titleBarRow,
            { 0.0, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel });
        return windowApplied && titleBarApplied;
    }

    [[nodiscard]] bool TryRestoreWindowedPresentation(
        HWND window,
        WindowedPresentation const& presentation,
        winrt::Microsoft::UI::Xaml::Controls::RowDefinition const& titleBarRow) noexcept
    {
        auto const windowRestored = TryRestoreWindow(window, presentation);
        auto const titleBarRestored = TrySetTitleBarHeight(titleBarRow, presentation.TitleBarHeight);
        return windowRestored && titleBarRestored;
    }
} // namespace

namespace HaloDesktop::Shell
{
    void WindowPresentationService::Attach(
        std::uintptr_t windowHandle,
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
        if (m_windowHandle != 0)
        {
            if (m_fullscreen)
            {
                static_cast<void>(TrySetFullscreen(false));
            }
            if (m_taskbarFullscreenMarked &&
                TryMarkTaskbarFullscreen(NativeWindow(m_windowHandle), false))
            {
                m_taskbarFullscreenMarked = false;
            }
        }

        m_windowHandle = 0;
        m_titleBarRow = nullptr;
        m_activation = WindowActivation::Active;
        m_windowedPlacement = { sizeof(WINDOWPLACEMENT) };
        m_windowedBounds = {};
        m_windowedStyle = 0;
        m_windowedExtendedStyle = 0;
        m_windowedTitleBarHeight = {};
        m_wasMaximized = false;
        m_fullscreen = false;
        m_taskbarFullscreenMarked = false;
    }

    bool WindowPresentationService::TrySetFullscreen(bool fullscreen) noexcept
    {
        if (m_windowHandle == 0 || !m_titleBarRow)
        {
            return false;
        }
        if (m_fullscreen == fullscreen)
        {
            RefreshFullscreenShellState();
            return true;
        }

        auto const window = NativeWindow(m_windowHandle);
        if (fullscreen)
        {
            auto const windowed = TryCaptureWindowedPresentation(window, m_titleBarRow);
            if (!windowed)
            {
                return false;
            }

            auto const fullscreenPolicy =
                CalculateFullscreenWindowPolicy(windowed->Style, windowed->ExtendedStyle);
            if (!TryApplyFullscreenPresentation(window, fullscreenPolicy, m_titleBarRow, m_activation))
            {
                if (TryRestoreWindowedPresentation(window, *windowed, m_titleBarRow))
                {
                    m_fullscreen = ResolveFullscreenState(
                        m_fullscreen,
                        fullscreen,
                        FullscreenTransitionOutcome::Failed);
                    return false;
                }
                if (!TryApplyFullscreenPresentation(window, fullscreenPolicy, m_titleBarRow, m_activation))
                {
                    static_cast<void>(TryRestoreWindowedPresentation(window, *windowed, m_titleBarRow));
                    m_fullscreen = ResolveFullscreenState(
                        m_fullscreen,
                        fullscreen,
                        FullscreenTransitionOutcome::Failed);
                    return false;
                }
            }

            m_windowedPlacement = windowed->Placement;
            m_windowedBounds = windowed->Bounds;
            m_windowedStyle = windowed->Style;
            m_windowedExtendedStyle = windowed->ExtendedStyle;
            m_windowedTitleBarHeight = windowed->TitleBarHeight;
            m_wasMaximized = windowed->WasMaximized;
            m_fullscreen = ResolveFullscreenState(
                m_fullscreen,
                fullscreen,
                FullscreenTransitionOutcome::Succeeded);
            if (TryMarkTaskbarFullscreen(window, true))
            {
                m_taskbarFullscreenMarked = true;
            }
            return true;
        }

        WindowedPresentation const windowed{
            m_windowedPlacement,
            m_windowedBounds,
            m_windowedStyle,
            m_windowedExtendedStyle,
            m_windowedTitleBarHeight,
            m_wasMaximized,
        };
        if (!TryRestoreWindowedPresentation(window, windowed, m_titleBarRow))
        {
            auto const fullscreenPolicy =
                CalculateFullscreenWindowPolicy(windowed.Style, windowed.ExtendedStyle);
            if (TryApplyFullscreenPresentation(window, fullscreenPolicy, m_titleBarRow, m_activation))
            {
                RefreshFullscreenShellState();
                m_fullscreen = ResolveFullscreenState(
                    m_fullscreen,
                    fullscreen,
                    FullscreenTransitionOutcome::Failed);
                return false;
            }
            if (!TryRestoreWindowedPresentation(window, windowed, m_titleBarRow))
            {
                static_cast<void>(TryApplyFullscreenPresentation(window, fullscreenPolicy, m_titleBarRow, m_activation));
                RefreshFullscreenShellState();
                m_fullscreen = ResolveFullscreenState(
                    m_fullscreen,
                    fullscreen,
                    FullscreenTransitionOutcome::Failed);
                return false;
            }
        }

        if (TryMarkTaskbarFullscreen(window, false))
        {
            m_taskbarFullscreenMarked = false;
        }

        m_windowedPlacement = { sizeof(WINDOWPLACEMENT) };
        m_windowedBounds = {};
        m_windowedStyle = 0;
        m_windowedExtendedStyle = 0;
        m_windowedTitleBarHeight = {};
        m_wasMaximized = false;
        m_fullscreen = ResolveFullscreenState(
            m_fullscreen,
            fullscreen,
            FullscreenTransitionOutcome::Succeeded);
        return true;
    }

    void WindowPresentationService::SetWindowActivation(WindowActivation activation) noexcept
    {
        m_activation = activation;
        if (m_windowHandle == 0 || !m_fullscreen)
        {
            return;
        }
        ApplyFullscreenZOrder();
        if (activation == WindowActivation::Active)
        {
            RefreshFullscreenShellState();
        }
    }

    void WindowPresentationService::ApplyFullscreenZOrder() noexcept
    {
        // A failed z-order change leaves the window where it is. Fullscreen
        // state itself is unaffected, and the next activation retries it.
        static_cast<void>(
            TrySetFullscreenZOrder(NativeWindow(m_windowHandle), m_fullscreenZOrder, m_activation));
    }

    void WindowPresentationService::RefreshFullscreenShellState() noexcept
    {
        if (m_windowHandle == 0)
        {
            return;
        }

        auto const window = NativeWindow(m_windowHandle);
        if (m_fullscreen && !m_taskbarFullscreenMarked)
        {
            if (TryMarkTaskbarFullscreen(window, true))
            {
                m_taskbarFullscreenMarked = true;
            }
            return;
        }
        if (!m_fullscreen && m_taskbarFullscreenMarked &&
            TryMarkTaskbarFullscreen(window, false))
        {
            m_taskbarFullscreenMarked = false;
        }
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
