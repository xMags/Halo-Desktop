#pragma once

#include <windows.h>

namespace HaloDesktop::Shell
{
    // The shell decides whether a window counts as fullscreen only once, in
    // the SetWindowPos call that gives it the monitor rectangle, and only when
    // that window is the foreground window at that instant. Nothing re-runs
    // the check later: activating the window again or re-issuing the same
    // rectangle leaves the taskbar on top until the geometry changes again.
    // An explicit topmost z-order does not depend on that check, so it is held
    // for as long as Halo is the active window and released when another
    // application takes over, which keeps that application in front.
    enum class FullscreenZOrder
    {
        TopmostWhileActive,
    };

    struct FullscreenWindowPolicy final
    {
        LONG_PTR Style{};
        LONG_PTR ExtendedStyle{};
        FullscreenZOrder ZOrder{ FullscreenZOrder::TopmostWhileActive };
    };

    enum class WindowActivation
    {
        Active,
        Inactive,
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
    // Whether the fullscreen window should sit in the topmost band right now.
    [[nodiscard]] bool ResolveFullscreenTopmost(
        FullscreenZOrder zOrder,
        WindowActivation activation) noexcept;
} // namespace HaloDesktop::Shell
