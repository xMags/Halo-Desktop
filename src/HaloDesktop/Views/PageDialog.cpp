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

    winrt::Microsoft::UI::Xaml::Controls::ContentDialog MakeDeleteFromDeviceDialog(
        winrt::Microsoft::UI::Xaml::XamlRoot const& xamlRoot,
        winrt::Microsoft::UI::Xaml::ElementTheme theme)
    {
        auto dialog = MakeDialog(
            xamlRoot,
            theme,
            L"Delete from device?",
            L"This permanently removes the video and its subtitle sidecar from this device.");
        dialog.PrimaryButtonText(L"Delete");
        dialog.CloseButtonText(L"Cancel");
        return dialog;
    }
}
