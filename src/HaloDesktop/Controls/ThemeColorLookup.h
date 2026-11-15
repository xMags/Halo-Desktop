#pragma once

#include <optional>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.UI.h>

namespace HaloDesktop::Controls
{
    // The colour a theme token holds for the theme `scope` is actually drawn
    // in, for code that paints with the composition layer, where a
    // ThemeResource reference cannot follow. Reads the app's theme
    // dictionaries ("Default" for dark, "Light") rather than the application's
    // own theme, which stays fixed at startup while the theme service sets the
    // requested theme on the window's root. Empty when the key is not a colour.
    [[nodiscard]] std::optional<winrt::Windows::UI::Color> ThemeColor(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& scope,
        wchar_t const* key);

    // The same lookup for a theme the caller has already decided on: Light reads
    // the "Light" dictionary, anything else the dark "Default" one.
    [[nodiscard]] std::optional<winrt::Windows::UI::Color> ThemeColor(
        winrt::Microsoft::UI::Xaml::ElementTheme theme,
        wchar_t const* key);
}
