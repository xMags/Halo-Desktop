#pragma once

#include "Shell/WindowPresentationPolicy.h"

#include <cstdint>
#include <functional>
#include <windows.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace HaloDesktop::Shell
{
    // UI-thread-only adapter for window presentation. The service does not own
    // the window or title row and must be detached before XAML teardown.
    class WindowPresentationService final
    {
    public:
        // Turns the window's content-in-title-bar mode on (true) or off (false).
        // Fullscreen turns it off: while it is on, WinUI keeps a 1px top border
        // strip above the content and a title bar drag strip over the top of
        // the screen. Called on the UI thread; may throw.
        using ContentInTitleBarSetter = std::function<void(bool extendsContent)>;

        void Attach(std::uintptr_t windowHandle,
                    winrt::Microsoft::UI::Xaml::Controls::RowDefinition const& titleBarRow,
                    ContentInTitleBarSetter setContentInTitleBar);
        void Detach() noexcept;
        [[nodiscard]] bool TrySetFullscreen(bool fullscreen) noexcept;
        // Called on every window activation change. While fullscreen this moves
        // the window into or out of the topmost band so it covers the taskbar
        // only while Halo is the application in use.
        void SetWindowActivation(WindowActivation activation) noexcept;
        void RefreshFullscreenShellState() noexcept;
        [[nodiscard]] bool IsFullscreen() const noexcept;
        [[nodiscard]] std::uintptr_t WindowHandle() const;

    private:
        void ApplyFullscreenZOrder() noexcept;

        std::uintptr_t m_windowHandle{};
        WindowActivation m_activation{ WindowActivation::Active };
        FullscreenZOrder m_fullscreenZOrder{ FullscreenZOrder::TopmostWhileActive };
        winrt::Microsoft::UI::Xaml::Controls::RowDefinition m_titleBarRow{ nullptr };
        ContentInTitleBarSetter m_setContentInTitleBar;
        WINDOWPLACEMENT m_windowedPlacement{ sizeof(WINDOWPLACEMENT) };
        RECT m_windowedBounds{};
        LONG_PTR m_windowedStyle{};
        LONG_PTR m_windowedExtendedStyle{};
        winrt::Microsoft::UI::Xaml::GridLength m_windowedTitleBarHeight{};
        bool m_wasMaximized{};
        // Fullscreen was entered from maximized by leaving maximized in place;
        // leaving fullscreen re-maximizes and hands back this normal rectangle.
        bool m_restoreMaximized{};
        RECT m_savedNormalRect{};
        bool m_fullscreen{};
        bool m_taskbarFullscreenMarked{};
    };
} // namespace HaloDesktop::Shell
