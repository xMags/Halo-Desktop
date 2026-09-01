#pragma once

#include <cstdint>
#include <windows.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace HaloDesktop::Shell
{
    // UI-thread-only adapter for window presentation. The service does not own
    // the window or title row and must be detached before XAML teardown.
    class WindowPresentationService final
    {
    public:
        void Attach(std::uintptr_t windowHandle,
                    winrt::Microsoft::UI::Xaml::Controls::RowDefinition const& titleBarRow);
        void Detach() noexcept;
        [[nodiscard]] bool TrySetFullscreen(bool fullscreen) noexcept;
        void RefreshFullscreenShellState() noexcept;
        [[nodiscard]] bool IsFullscreen() const noexcept;
        [[nodiscard]] std::uintptr_t WindowHandle() const;

    private:
        std::uintptr_t m_windowHandle{};
        winrt::Microsoft::UI::Xaml::Controls::RowDefinition m_titleBarRow{ nullptr };
        WINDOWPLACEMENT m_windowedPlacement{ sizeof(WINDOWPLACEMENT) };
        RECT m_windowedBounds{};
        LONG_PTR m_windowedStyle{};
        LONG_PTR m_windowedExtendedStyle{};
        winrt::Microsoft::UI::Xaml::GridLength m_windowedTitleBarHeight{};
        bool m_wasMaximized{};
        bool m_fullscreen{};
        bool m_taskbarFullscreenMarked{};
    };
} // namespace HaloDesktop::Shell
