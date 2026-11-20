#pragma once

#include "Shell/WindowPlacementPolicy.h"

#include <cstdint>
#include <optional>
#include <wil/resource.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Graphics.h>

namespace HaloDesktop::Shell
{
    class WindowSizing final
    {
    public:
        explicit WindowSizing(winrt::Microsoft::UI::Xaml::Window const& window);
        ~WindowSizing();

        // Its window event handler refers back to it, so it stays where it is.
        WindowSizing(WindowSizing const&) = delete;
        WindowSizing& operator=(WindowSizing const&) = delete;
        WindowSizing(WindowSizing&&) = delete;
        WindowSizing& operator=(WindowSizing&&) = delete;

        [[nodiscard]] winrt::Microsoft::UI::Windowing::AppWindow const& AppWindow() const noexcept;
        [[nodiscard]] std::uintptr_t WindowHandle() const noexcept;

        // Puts the window back as it was left, before it is first shown: its
        // normal bounds now (when they still lie on a screen, RestorableBounds),
        // maximizing waits for ApplyPendingMaximize. Keeps the default size
        // when there is nothing usable to restore.
        void Restore(std::optional<SavedWindowPlacement> const& saved) noexcept;
        // Maximizes a window saved maximized, once it has been shown.
        void ApplyPendingMaximize() noexcept;
        // The window as it stands, for saving at close; nothing when Windows
        // cannot say (the window is already gone).
        [[nodiscard]] std::optional<SavedWindowPlacement> CurrentPlacement() const noexcept;

    private:
        // Matches the icon resource declared in HaloDesktop.rc. Windows shows
        // the lowest-numbered group icon as the executable's icon, so both the
        // window and Explorer end up on the same artwork.
        static constexpr int ApplicationIconResourceId{ 1 };
        // The edge the taskbar draws a window's icon at, in logical pixels.
        static constexpr int TaskbarIconDips{ 24 };

        // Gives the window icons cut for the display it is on, unless it
        // already holds the ones for that display's scale.
        void ApplyWindowIcon() noexcept;

        winrt::Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
        std::uintptr_t m_windowHandle{};
        // The icons the window currently shows, owned here because Windows
        // only borrows what WM_SETICON hands it, and the DPI they were cut for.
        wil::unique_hicon m_taskbarIcon;
        wil::unique_hicon m_captionIcon;
        unsigned int m_iconDpi{};
        // AppWindow gives out no weak reference, so its events cannot be
        // auto-revoked: the destructor takes this handler off by its token.
        winrt::event_token m_changedToken{};
        // The smallest window, in physical pixels at the window's first DPI.
        winrt::Windows::Graphics::SizeInt32 m_minimumSize{};
        bool m_pendingMaximize{};
    };
} // namespace HaloDesktop::Shell
