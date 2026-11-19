#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace HaloDesktop::Views
{
    // Home's featured hero draws a title's logo in place of its name. The logo
    // is fetched and decoded before it is shown, and judged on the way: logos
    // are drawn for dark backdrops, so a mostly white one would vanish over
    // the light theme's pale side of the hero. There it is drawn in the
    // theme's text ink instead. No UI or WinRT types here, so the tests
    // compile this by path.

    // The most a logo download may be. Logos are small marks; anything larger
    // is not one, and is refused rather than held in memory.
    inline constexpr std::size_t HeroLogoMaximumBytes = 4u * 1024u * 1024u;
    // The width the logo is sampled at for its brightness: enough to tell a
    // white mark from a dark one, cheap at any source size.
    inline constexpr std::uint32_t HeroLogoSampleWidth = 96;
    // The widest the logo is kept at for the inked copy: the hero shows it
    // at most 380 wide, so this covers a dense display.
    inline constexpr std::uint32_t HeroLogoInkWidth = 1140;
    // Brighter than this, a logo reads as white on the light theme's pale ground.
    inline constexpr double HeroLogoLightThemeMaximumLuminance = 0.55;
    // A logo covering more of its box than this is a picture with its own
    // background, not a mark: inked, it would be a solid block. Heavy block
    // lettering covers up to about 70%, so the line sits well above that.
    inline constexpr double HeroLogoInkMaximumCoverage = 0.9;

    // Only an absolute https address is fetched.
    [[nodiscard]] bool IsHeroLogoUrl(std::wstring_view url) noexcept;

    // The mean brightness (0 black to 1 white) of the logo's visible pixels,
    // each weighted by its opacity, from straight-alpha BGRA8 pixels. Empty
    // when nothing is visible, or the bytes are not whole pixels.
    [[nodiscard]] std::optional<double> HeroLogoLuminance(std::span<std::uint8_t const> bgra) noexcept;

    // The share of the box the logo covers: its pixels' mean opacity, 0 to 1.
    // Empty when the bytes are not whole pixels.
    [[nodiscard]] std::optional<double> HeroLogoCoverage(std::span<std::uint8_t const> bgra) noexcept;

    enum class HeroLogoTreatment
    {
        // The logo as its artist drew it.
        Original,
        // Its shape in the theme's text ink: a light mark on the light theme.
        Inked,
        // No logo: the title's name.
        Name,
    };

    // How the hero draws a logo of that brightness and coverage. The dark
    // theme always shows it as drawn. The light theme shows a dark enough one
    // as drawn, inks a light mark, and keeps the name for a light logo that
    // fills its box, or one it could not judge.
    [[nodiscard]] HeroLogoTreatment ChooseHeroLogoTreatment(
        std::optional<double> luminance,
        std::optional<double> coverage,
        bool lightTheme) noexcept;

    // Turns straight-alpha BGRA8 pixels into the same shape in one colour, as
    // premultiplied BGRA8 (what a writeable bitmap holds), in place.
    void InkHeroLogo(std::span<std::uint8_t> bgra, std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept;
}
