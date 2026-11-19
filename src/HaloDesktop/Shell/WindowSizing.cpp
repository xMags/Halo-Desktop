#include "pch.h"
#include "Shell/WindowSizing.h"

#include <microsoft.ui.xaml.window.h>
#include <utility>
#include <vector>
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

    // Every screen's work area, in the coordinates GetWindowPlacement keeps a
    // window's normal bounds in: screen coordinates offset by the primary
    // screen's work area origin (a taskbar on the top or left moves it).
    std::vector<HaloDesktop::Shell::WindowRect> WorkspaceWorkAreas()
    {
        MONITORINFO primary{ sizeof(MONITORINFO) };
        if (!GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &primary))
        {
            return {};
        }
        auto const offsetX = primary.rcWork.left - primary.rcMonitor.left;
        auto const offsetY = primary.rcWork.top - primary.rcMonitor.top;

        struct Collected final
        {
            std::vector<HaloDesktop::Shell::WindowRect> Areas;
            LONG OffsetX{};
            LONG OffsetY{};
        } collected{ {}, offsetX, offsetY };
        auto const collect = [](HMONITOR monitor, HDC, LPRECT, LPARAM state) -> BOOL
        {
            auto& into = *reinterpret_cast<Collected*>(state);
            MONITORINFO info{ sizeof(MONITORINFO) };
            if (GetMonitorInfoW(monitor, &info))
            {
                into.Areas.push_back({
                    info.rcWork.left - into.OffsetX,
                    info.rcWork.top - into.OffsetY,
                    info.rcWork.right - into.OffsetX,
                    info.rcWork.bottom - into.OffsetY,
                });
            }
            return TRUE;
        };
        EnumDisplayMonitors(nullptr, nullptr, collect, reinterpret_cast<LPARAM>(&collected));
        return std::move(collected.Areas);
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
        m_minimumSize = minimumSize;
        m_appWindow.Resize(defaultSize);

        auto const presenter = m_appWindow.Presenter().as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>();
        presenter.PreferredMinimumWidth(BoxDimension(minimumSize.Width));
        presenter.PreferredMinimumHeight(BoxDimension(minimumSize.Height));

        ApplyWindowIcon();
    }

    // A WinUI window starts with no icon of its own, and the shell does not
    // fall back to the executable's icon for it, so the taskbar and Alt+Tab
    // show a placeholder until one is set explicitly. The icon is loaded from
    // this executable's own resources rather than a file so there is nothing
    // extra to install alongside the binary.
    void WindowSizing::ApplyWindowIcon() noexcept
    {
        // LR_SHARED hands back a cached handle owned by the system, which must
        // not be destroyed. That matches the lifetime wanted here: one icon for
        // as long as the process runs.
        auto* const icon = static_cast<HICON>(LoadImageW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(ApplicationIconResourceId),
            IMAGE_ICON,
            0,
            0,
            LR_DEFAULTSIZE | LR_SHARED));
        if (!icon)
        {
            // A missing icon is cosmetic. Never fail window creation over it.
            return;
        }

        m_appWindow.SetIcon(winrt::Microsoft::UI::GetIconIdFromIcon(icon));
    }

    void WindowSizing::Restore(std::optional<SavedWindowPlacement> const& saved) noexcept
    {
        if (!saved)
        {
            return;
        }
        try
        {
            auto const window = reinterpret_cast<HWND>(m_windowHandle);
            auto const workAreas = WorkspaceWorkAreas();
            auto const bounds = RestorableBounds(saved->Normal, workAreas, m_minimumSize.Width, m_minimumSize.Height);
            if (!bounds)
            {
                return;
            }
            WINDOWPLACEMENT placement{ sizeof(WINDOWPLACEMENT) };
            if (!GetWindowPlacement(window, &placement))
            {
                return;
            }
            // Bounds only: the window is not shown yet, and showing it is the
            // app's first activation, which shows it at these normal bounds.
            placement.showCmd = SW_HIDE;
            placement.rcNormalPosition = RECT{ bounds->Left, bounds->Top, bounds->Right, bounds->Bottom };
            if (SetWindowPlacement(window, &placement))
            {
                m_pendingMaximize = saved->Maximized;
            }
        }
        catch (...)
        {
            // A window that cannot be put back opens at its default size.
        }
    }

    void WindowSizing::ApplyPendingMaximize() noexcept
    {
        if (!m_pendingMaximize)
        {
            return;
        }
        m_pendingMaximize = false;
        try
        {
            m_appWindow.Presenter().as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>().Maximize();
        }
        catch (...)
        {
        }
    }

    std::optional<SavedWindowPlacement> WindowSizing::CurrentPlacement() const noexcept
    {
        WINDOWPLACEMENT placement{ sizeof(WINDOWPLACEMENT) };
        if (!m_windowHandle || !GetWindowPlacement(reinterpret_cast<HWND>(m_windowHandle), &placement))
        {
            return std::nullopt;
        }
        auto const& normal = placement.rcNormalPosition;
        if (normal.right <= normal.left || normal.bottom <= normal.top)
        {
            return std::nullopt;
        }
        return SavedWindowPlacement{
            .Normal = { normal.left, normal.top, normal.right, normal.bottom },
            .Maximized = SavesAsMaximized(
                placement.showCmd == SW_SHOWMAXIMIZED,
                placement.showCmd == SW_SHOWMINIMIZED,
                (placement.flags & WPF_RESTORETOMAXIMIZED) != 0),
        };
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
