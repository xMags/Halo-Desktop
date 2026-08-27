#include "Shell/WindowPresentationPolicy.h"

namespace HaloDesktop::Shell
{
    FullscreenWindowPolicy CalculateFullscreenWindowPolicy(
        LONG_PTR windowedStyle,
        LONG_PTR windowedExtendedStyle) noexcept
    {
        return {
            (windowedStyle & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) |
                static_cast<LONG_PTR>(WS_POPUP),
            windowedExtendedStyle &
                ~static_cast<LONG_PTR>(WS_EX_WINDOWEDGE) &
                ~static_cast<LONG_PTR>(WS_EX_TOPMOST),
            FullscreenZOrder::ForegroundNonTopmost,
        };
    }

    bool ResolveFullscreenState(
        bool currentFullscreen,
        bool requestedFullscreen,
        FullscreenTransitionOutcome outcome) noexcept
    {
        return outcome == FullscreenTransitionOutcome::Succeeded
            ? requestedFullscreen
            : currentFullscreen;
    }

    void PlayerOverlayMoveState::Reset() noexcept
    {
        m_active = false;
    }

    bool PlayerOverlayMoveState::Enter() noexcept
    {
        auto const changed = !m_active;
        m_active = true;
        return changed;
    }

    bool PlayerOverlayMoveState::Exit(PlayerOverlayLifecycle lifecycle) noexcept
    {
        auto const wasActive = m_active;
        m_active = false;
        return wasActive && lifecycle == PlayerOverlayLifecycle::Ready;
    }

    bool PlayerOverlayMoveState::CanOpen(PlayerOverlayLifecycle lifecycle) const noexcept
    {
        return lifecycle == PlayerOverlayLifecycle::Ready && !m_active;
    }

    bool PlayerOverlayMoveState::IsActive() const noexcept
    {
        return m_active;
    }
} // namespace HaloDesktop::Shell
