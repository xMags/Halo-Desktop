#pragma once

#include "Shell/WindowPlacementPolicy.h"

#include <cstdint>
#include <optional>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Graphics.h>

namespace HaloDesktop::Shell
{
    class WindowSizing final
    {
    public:
        explicit WindowSizing(winrt::Microsoft::UI::Xaml::Window const& window);

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

        void ApplyWindowIcon() noexcept;

        winrt::Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
        std::uintptr_t m_windowHandle{};
        // The smallest window, in physical pixels at the window's first DPI.
        winrt::Windows::Graphics::SizeInt32 m_minimumSize{};
        bool m_pendingMaximize{};
    };
} // namespace HaloDesktop::Shell
