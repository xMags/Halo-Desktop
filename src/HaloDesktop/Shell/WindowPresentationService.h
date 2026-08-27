#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <windows.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace HaloDesktop::Shell
{
    using MoveSizeChangedToken = std::uint64_t;
    using MoveSizeChangedHandler = std::function<void(bool)>;

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
        [[nodiscard]] MoveSizeChangedToken AddMoveSizeChangedHandler(MoveSizeChangedHandler handler);
        void RemoveMoveSizeChangedHandler(MoveSizeChangedToken token) noexcept;
        [[nodiscard]] bool IsMoveSizeActive() const noexcept;

    private:
        void AttachMoveSizeSource();
        void DetachMoveSizeSource() noexcept;
        void SetMoveSizeActive(bool active) noexcept;

        std::uintptr_t m_windowHandle{};
        winrt::Microsoft::UI::Xaml::Controls::RowDefinition m_titleBarRow{ nullptr };
        winrt::Microsoft::UI::Input::InputNonClientPointerSource m_nonClientPointerSource{ nullptr };
        winrt::event_token m_enteringMoveSizeToken{};
        winrt::event_token m_exitedMoveSizeToken{};
        std::unordered_map<MoveSizeChangedToken, MoveSizeChangedHandler> m_moveSizeHandlers;
        MoveSizeChangedToken m_nextMoveSizeToken{ 1 };
        WINDOWPLACEMENT m_windowedPlacement{ sizeof(WINDOWPLACEMENT) };
        RECT m_windowedBounds{};
        LONG_PTR m_windowedStyle{};
        LONG_PTR m_windowedExtendedStyle{};
        winrt::Microsoft::UI::Xaml::GridLength m_windowedTitleBarHeight{};
        bool m_wasMaximized{};
        bool m_fullscreen{};
        bool m_taskbarFullscreenMarked{};
        bool m_moveSizeActive{};
    };
} // namespace HaloDesktop::Shell
