#pragma once

#include <windows.h>

namespace HaloDesktop::Shell
{
    enum class FullscreenZOrder
    {
        ForegroundNonTopmost,
    };

    struct FullscreenWindowPolicy final
    {
        LONG_PTR Style{};
        LONG_PTR ExtendedStyle{};
        FullscreenZOrder ZOrder{ FullscreenZOrder::ForegroundNonTopmost };
    };

    enum class FullscreenTransitionOutcome
    {
        Succeeded,
        Failed,
    };

    [[nodiscard]] FullscreenWindowPolicy CalculateFullscreenWindowPolicy(
        LONG_PTR windowedStyle,
        LONG_PTR windowedExtendedStyle) noexcept;
    [[nodiscard]] bool ResolveFullscreenState(
        bool currentFullscreen,
        bool requestedFullscreen,
        FullscreenTransitionOutcome outcome) noexcept;

    enum class PlayerOverlayLifecycle
    {
        Unloaded,
        Ready,
        Closing,
    };

    // Keeps Popup visibility decisions deterministic across the native modal
    // move-size loop. Player lifetime remains owned by PlayerPage; this state
    // only records whether that loop currently forbids opening its overlay.
    class PlayerOverlayMoveState final
    {
    public:
        void Reset() noexcept;
        [[nodiscard]] bool Enter() noexcept;
        [[nodiscard]] bool Exit(PlayerOverlayLifecycle lifecycle) noexcept;
        [[nodiscard]] bool CanOpen(PlayerOverlayLifecycle lifecycle) const noexcept;
        [[nodiscard]] bool IsActive() const noexcept;

    private:
        bool m_active{};
    };
} // namespace HaloDesktop::Shell
