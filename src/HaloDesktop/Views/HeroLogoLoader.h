#pragma once

#include <cstdint>
#include <optional>
#include <ppltasks.h>
#include <string>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Web.Http.h>

namespace HaloDesktop::Views
{
    // A featured title's logo, fetched and decoded before the hero shows it.
    struct LoadedHeroLogo final
    {
        // Already holds the picture: shown, it appears at once.
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage Image{ nullptr };
        // HeroLogoLuminance and HeroLogoCoverage of a small sample, empty
        // when they could not be judged.
        std::optional<double> Luminance;
        std::optional<double> Coverage;
        // Straight-alpha BGRA8 at no more than HeroLogoInkWidth, for an inked
        // copy (InkHeroLogo); empty when the logo could not be decoded so.
        std::vector<std::uint8_t> Pixels;
        std::uint32_t PixelWidth{};
        std::uint32_t PixelHeight{};
        // The inked copy, made the first time the light theme needs it.
        winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap Inked{ nullptr };
        winrt::Windows::UI::Color InkColor{};
    };

    // Downloads the logo at `url` (an https address, at most
    // HeroLogoMaximumBytes), samples its brightness and hands it to a bitmap.
    // Call it on the UI thread; it finishes there, since the bitmap belongs to
    // it. Throws when the logo cannot be fetched or decoded.
    [[nodiscard]] concurrency::task<LoadedHeroLogo> LoadHeroLogoAsync(
        winrt::Windows::Web::Http::HttpClient client,
        std::wstring url);

    // The logo's shape in `ink`, made once per logo and ink and kept on it.
    // Empty when the logo kept no pixels or XAML refuses the bitmap. UI thread.
    [[nodiscard]] winrt::Microsoft::UI::Xaml::Media::ImageSource InkedHeroLogo(
        LoadedHeroLogo& logo,
        winrt::Windows::UI::Color ink);
}
