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
} // namespace HaloDesktop::Shell
