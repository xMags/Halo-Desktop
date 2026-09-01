#include "pch.h"
#include "Views/PageDialog.h"

namespace HaloDesktop::Views
{
    winrt::Microsoft::UI::Xaml::Controls::ContentDialog MakeDialog(
        winrt::Microsoft::UI::Xaml::XamlRoot const& xamlRoot,
        winrt::Microsoft::UI::Xaml::ElementTheme theme,
        winrt::hstring const& title,
        winrt::hstring const& body)
    {
        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog;
        dialog.XamlRoot(xamlRoot);
        dialog.RequestedTheme(theme);
        dialog.Title(winrt::box_value(title));
        dialog.Content(winrt::box_value(body));
        return dialog;
    }
}
