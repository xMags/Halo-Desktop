#pragma once

#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Controls
{
    // A ScrollViewer that can only scroll horizontally turns vertical wheel deltas into
    // sideways panning, so hovering a shelf hijacks the wheel from the page beneath it.
    // Call this from a wheel handler on the horizontal scroller's content: it claims the
    // event before the scroller sees it and applies the delta to the nearest ancestor
    // that scrolls vertically, keeping the wheel on one axis everywhere in the app.
    void RedirectWheelToVerticalAncestor(
        winrt::Microsoft::UI::Xaml::UIElement const& source,
        winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
}
