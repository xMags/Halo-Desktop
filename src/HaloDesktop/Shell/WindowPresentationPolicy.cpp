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
} // namespace HaloDesktop::Shell
