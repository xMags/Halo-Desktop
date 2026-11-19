#pragma once

#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Controls
{
    // How far a card sinks while it is pressed, as Windows 11's own controls
    // give a press, and how long it takes to sink and to spring back.
    inline constexpr float PressedScale = 0.97F;

    // Makes `target` sink to PressedScale, about its centre, while a pointer
    // is pressed on `source` (usually the card's own button, which handles
    // the press itself, so this listens to handled presses too), and spring
    // back when the press ends, is cancelled, loses its capture or leaves.
    // Does nothing while Windows' animation effects are off. The handlers
    // hold `target` weakly, so they never keep a card alive.
    //
    // `target` takes a ScaleTransition and its Scale; it must not also be
    // handed a composition visual, which XAML refuses alongside them.
    void AttachPressFeedback(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& target,
        winrt::Microsoft::UI::Xaml::UIElement const& source);
}
