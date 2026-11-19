#pragma once

#include <optional>
#include <ppltasks.h>
#include <string>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Web.Http.h>

namespace HaloDesktop::Views
{
    // A featured title's logo, fetched and decoded before the hero shows it.
    struct LoadedHeroLogo final
    {
        // Already holds the picture: shown, it appears at once.
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage Image{ nullptr };
        // HeroLogoLuminance of a small sample, empty when it could not be judged.
        std::optional<double> Luminance;
    };

    // Downloads the logo at `url` (an https address, at most
    // HeroLogoMaximumBytes), samples its brightness and hands it to a bitmap.
    // Call it on the UI thread; it finishes there, since the bitmap belongs to
    // it. Throws when the logo cannot be fetched or decoded.
    [[nodiscard]] concurrency::task<LoadedHeroLogo> LoadHeroLogoAsync(
        winrt::Windows::Web::Http::HttpClient client,
        std::wstring url);
}
