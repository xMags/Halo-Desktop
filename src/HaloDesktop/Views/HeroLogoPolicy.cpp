#include "Views/HeroLogoPolicy.h"

namespace
{
    // Below this share of fully opaque pixels the logo is too faint to judge.
    constexpr double MinimumCoverage = 0.002;
}

namespace HaloDesktop::Views
{
    bool IsHeroLogoUrl(std::wstring_view url) noexcept
    {
        constexpr std::wstring_view Scheme = L"https://";
        if (url.size() <= Scheme.size()) return false;
        for (std::size_t index = 0; index < Scheme.size(); ++index)
        {
            auto character = url[index];
            if (character >= L'A' && character <= L'Z') character = static_cast<wchar_t>(character - L'A' + L'a');
            if (character != Scheme[index]) return false;
        }
        return url.find_first_of(L" \t\r\n") == std::wstring_view::npos;
    }

    std::optional<double> HeroLogoLuminance(std::span<std::uint8_t const> bgra) noexcept
    {
        if (bgra.empty() || bgra.size() % 4 != 0) return std::nullopt;
        double weighted = 0.0;
        double weight = 0.0;
        for (std::size_t index = 0; index < bgra.size(); index += 4)
        {
            auto const alpha = bgra[index + 3] / 255.0;
            if (alpha <= 0.0) continue;
            // Rec. 709 luma of the pixel's own colour, as straight alpha keeps it.
            auto const luma = (0.0722 * bgra[index] + 0.7152 * bgra[index + 1] + 0.2126 * bgra[index + 2]) / 255.0;
            weighted += luma * alpha;
            weight += alpha;
        }
        auto const pixels = static_cast<double>(bgra.size() / 4);
        if (weight <= 0.0 || weight / pixels < MinimumCoverage) return std::nullopt;
        return weighted / weight;
    }

    bool HeroLogoReadable(std::optional<double> luminance, bool lightTheme) noexcept
    {
        if (!lightTheme) return true;
        return luminance && *luminance <= HeroLogoLightThemeMaximumLuminance;
    }
}
