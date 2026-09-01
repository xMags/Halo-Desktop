#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.h>

namespace HaloDesktop::Views
{
    // A ContentDialog is hosted in the XamlRoot's popup root, which is a sibling of the
    // content root rather than a descendant of it, so the RequestedTheme the theme
    // service sets on the shell never reaches one: a dialog raised from the dark app on
    // a light desktop opens light. Every dialog is built here and told the theme its
    // caller resolved to, so all of them open in the theme the user is looking at.
    //
    // Callers pass the XamlRoot and theme rather than the page itself because a page can
    // raise a dialog after an await, when it must use the values it captured beforehand.
    //
    // Named MakeDialog rather than CreateDialog: windows.h defines CreateDialog as a
    // macro, and any call to a function of that name expands into CreateDialogParamW.
    [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::ContentDialog MakeDialog(
        winrt::Microsoft::UI::Xaml::XamlRoot const& xamlRoot,
        winrt::Microsoft::UI::Xaml::ElementTheme theme,
        winrt::hstring const& title,
        winrt::hstring const& body);
}
