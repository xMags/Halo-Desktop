#pragma once

#include <winrt/Microsoft.UI.Xaml.Input.h>

namespace HaloDesktop::Controls
{
    // The one wheel rule for every sideways strip in the app. A strip is a
    // ScrollViewer with HorizontalScrollMode="Disabled", which keeps a plain wheel
    // going on to the page, and whose content root (a panel with a transparent
    // background, so the gaps between cards answer too) forwards its
    // PointerWheelChanged here. That mode alone does not stop a tilt wheel or
    // Shift with the wheel from panning the strip, so this takes those turns.
    //
    // The handler belongs on the content, never on the ScrollViewer: the viewer
    // class-handles the wheel before a handler attached to itself runs.
    // StabilityTests scans every XAML file for strips that miss either half.
    void GuardSidewaysWheel(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) noexcept;
}
