#pragma once

#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace HaloDesktop::Controls
{
    // Gives a ScrollView browser-style wheel scrolling. Each notch adds
    // velocity to the view's own inertia on the compositor, so turning the
    // wheel keeps the page gliding at an even speed instead of restarting an
    // animation per notch, and the distance follows Windows' "lines per
    // notch" setting (WheelLineDistance per line).
    //
    // The view stops handling the wheel itself (IgnoredInputKinds); touch,
    // touchpad panning, keys and the scrollbar are left to it. A sideways
    // wheel and Ctrl with the wheel are left alone. The handler holds the view
    // weakly. Attaching a view again does nothing, so a page can call this on
    // every Loaded. UI thread only.
    void AttachSmoothWheelScroll(winrt::Microsoft::UI::Xaml::Controls::ScrollView const& view);

    // How far one notch travels for each of Windows' wheel lines, in view
    // pixels, and how quickly the glide dies away (the share of its speed lost
    // per second). Three lines, Windows' default, make 120px a notch, about
    // what a browser scrolls.
    inline constexpr double WheelLineDistance = 40.0;
    inline constexpr float WheelInertiaDecay = 0.999F;

    // The velocity, in pixels per second, that travels `distance` before the
    // glide dies away at WheelInertiaDecay.
    [[nodiscard]] double WheelVelocityFor(double distance) noexcept;
}
