#include "pch.h"
#include "Views/PageDialog.h"

namespace HaloDesktop::Views
{
    winrt::Microsoft::UI::Xaml::Controls::ContentDialog MakeDialog(
        winrt::Microsoft::UI::Xaml::XamlRoot const& xamlRoot,
        winrt::Microsoft::UI::Xaml::ElementTheme theme,
        winrt::hstring const& title,
        winrt::hstring const& body,
        DialogTone tone)
    {
        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog;
        dialog.XamlRoot(xamlRoot);
        dialog.RequestedTheme(theme);
        dialog.Title(winrt::box_value(title));
        dialog.Content(winrt::box_value(body));
        if (tone == DialogTone::Destructive)
        {
            // The style's brushes are theme resources, so they resolve under the
            // RequestedTheme set above rather than the application's theme.
            dialog.PrimaryButtonStyle(
                winrt::Microsoft::UI::Xaml::Application::Current()
                    .Resources()
                    .Lookup(winrt::box_value(L"HaloDangerButtonStyle"))
                    .as<winrt::Microsoft::UI::Xaml::Style>());
        }
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
            L"This permanently removes the video and its subtitle sidecar from this device.",
            DialogTone::Destructive);
        dialog.PrimaryButtonText(L"Delete");
        dialog.CloseButtonText(L"Cancel");
        return dialog;
    }
}
