#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace HaloDesktop::Shell
{
    // A window rectangle in physical pixels, in the coordinates Windows keeps a
    // window's restored position in (GetWindowPlacement's workspace space).
    struct WindowRect final
    {
        std::int32_t Left{};
        std::int32_t Top{};
        std::int32_t Right{};
        std::int32_t Bottom{};

        [[nodiscard]] std::int64_t Width() const noexcept { return std::int64_t{ Right } - Left; }
        [[nodiscard]] std::int64_t Height() const noexcept { return std::int64_t{ Bottom } - Top; }

        bool operator==(WindowRect const&) const = default;
    };

    // How the window was left at close: its normal (not maximized) bounds and
    // whether it was maximized.
    struct SavedWindowPlacement final
    {
        WindowRect Normal;
        bool Maximized{};

        bool operator==(SavedWindowPlacement const&) const = default;
    };

    // Whether a window closed in this state should come back maximized. A
    // window minimized at close comes back as it was before it was minimized.
    [[nodiscard]] bool SavesAsMaximized(bool maximized, bool minimized, bool restoresToMaximized) noexcept;

    // Where a saved window may go back to, given the work areas of the screens
    // attached now (in the same coordinates). It goes back where it was if
    // enough of its title strip lies on one of them to drag it by; it is grown
    // to the minimum size and shrunk to fit the screen it is on. Nothing when
    // the saved bounds are not a real window or lie off every screen (a monitor
    // unplugged since), so the caller keeps its default size instead.
    [[nodiscard]] std::optional<WindowRect> RestorableBounds(
        WindowRect const& saved,
        std::span<WindowRect const> workAreas,
        std::int32_t minimumWidth,
        std::int32_t minimumHeight) noexcept;
}
