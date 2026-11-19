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
    // the light theme's pale side of the hero. No UI or WinRT types here, so
    // the tests compile this by path.

    // The most a logo download may be. Logos are small marks; anything larger
    // is not one, and is refused rather than held in memory.
    inline constexpr std::size_t HeroLogoMaximumBytes = 4u * 1024u * 1024u;
    // The width the logo is sampled at for its brightness: enough to tell a
    // white mark from a dark one, cheap at any source size.
    inline constexpr std::uint32_t HeroLogoSampleWidth = 96;
    // Brighter than this, a logo reads as white on the light theme's pale ground.
    inline constexpr double HeroLogoLightThemeMaximumLuminance = 0.55;

    // Only an absolute https address is fetched.
    [[nodiscard]] bool IsHeroLogoUrl(std::wstring_view url) noexcept;

    // The mean brightness (0 black to 1 white) of the logo's visible pixels,
    // each weighted by its opacity, from straight-alpha BGRA8 pixels. Empty
    // when nothing is visible, or the bytes are not whole pixels.
    [[nodiscard]] std::optional<double> HeroLogoLuminance(std::span<std::uint8_t const> bgra) noexcept;

    // Whether a logo of that brightness stands out on the hero's text side:
    // always in the dark theme, as before; in the light theme only one dark
    // enough to read over the pale ground. A logo whose brightness is unknown
    // is shown in the dark theme only.
    [[nodiscard]] bool HeroLogoReadable(std::optional<double> luminance, bool lightTheme) noexcept;
}
