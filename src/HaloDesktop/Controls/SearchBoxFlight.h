#pragma once

#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Controls
{
    // Home's search box opening the Search page: the small box flies to where
    // the page's own search field sits and grows into it (a connected
    // animation), so the field reads as the same box getting bigger rather than
    // a new page sliding in.
    //
    // Call PrepareSearchBoxFlight on Home's box just before navigating, and
    // StartSearchBoxFlight from the Search page's OnNavigatedTo. Only a flight
    // prepared moments before can start; any other arrival (the nav item,
    // Ctrl+K, Back) finds none and returns false, and the page then arrives as
    // it always does. Windows' animation setting is honoured by XAML itself.
    // Neither call throws: a refusal from XAML just means no flight.
    void PrepareSearchBoxFlight(winrt::Microsoft::UI::Xaml::UIElement const& source) noexcept;

    // True when the flight to `destination` started.
    [[nodiscard]] bool StartSearchBoxFlight(winrt::Microsoft::UI::Xaml::UIElement const& destination) noexcept;
}
