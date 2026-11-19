#pragma once

#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Controls
{
    // Content that has just appeared or changed settles into place: it fades
    // in over 250ms while rising 8px, on Windows' decelerate curve, as
    // Windows' own lists and expanders do. Used when a folded section opens
    // and when a tab swaps what a page lists.
    //
    // Runs on `element`'s opacity and a TranslateTransform it is given (or
    // already has as its RenderTransform), never a composition visual, so it
    // can sit beside facade transitions elsewhere. A replay restarts it from
    // the beginning. Does nothing while Windows' animation effects are off, and a
    // refusal from XAML leaves the element fully shown.
    void PlayContentEntrance(winrt::Microsoft::UI::Xaml::UIElement const& element);

    // Plays the entrance when `element` has turned visible since the last call
    // (`wasShown` remembers, and is updated): content taking over from its
    // loading placeholders fades in rather than replacing them in one frame.
    // Call it after the bindings have updated, from a property-change handler.
    void PlayEntranceWhenShown(winrt::Microsoft::UI::Xaml::UIElement const& element, bool& wasShown);
}
