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

    // A maximized window keeps its maximized state through a style switch, and
    // Windows holds a maximized window to the work area, so fullscreen entered
    // from maximized can stop short of the taskbar. As the reference's
    // window_placement.rs, the window leaves maximized first, in place: its
    // normal rectangle is set to the work area it already fills and it is shown
    // normal, so nothing on screen moves. This is that rectangle in the
    // workspace coordinates SetWindowPlacement takes for a top-level window:
    // screen coordinates less the offset of the monitor's work area from the
    // monitor's corner. On the primary monitor this is (0, 0, width, height), the
    // reference's rectangle; on another monitor it keeps that monitor's origin.
    [[nodiscard]] RECT WorkAreaInWorkspaceCoordinates(RECT const& monitor, RECT const& workArea) noexcept;

    // Whether the window landed exactly where the in-place restore meant it to.
    // When it did not, the coordinate assumption above did not hold and the
    // restore is undone rather than leaving the window somewhere else.
    [[nodiscard]] bool IsSameRect(RECT const& left, RECT const& right) noexcept;
} // namespace HaloDesktop::Shell
