#pragma once

#include <cstddef>
#include <iterator>
#include <string_view>

namespace HaloDesktop::ViewModels
{
    // The stored vocabulary and preview arithmetic for subtitle appearance, shared by the
    // settings page and the player panel. Both surfaces edit the same account settings, so
    // a value stored by one has to read back the same way in the other; keeping the tables
    // and the offsets here is what stops them drifting apart.
    //
    // The font strings are what SettingsSyncService persists, and the order is the order
    // the chips appear in. "Default" leaves the face to the engine rather than naming one,
    // which SubtitleController::Font turns into an empty family.
    //
    // Header-only and free of WinRT so it stays out of the shared project files.

    inline constexpr std::wstring_view kSubtitleFontFamilies[]{
        L"Default",
        L"Segoe UI",
        L"Georgia",
        L"JetBrains Mono",
    };

    inline constexpr std::wstring_view kSubtitleOutlines[]{
        L"none",
        L"thin",
        L"normal",
        L"thick",
    };

    // Falls back to the system face, matching the settings service, which rewrites an
    // unusable stored family to Segoe UI rather than rejecting it.
    [[nodiscard]] inline std::size_t SubtitleFontIndex(std::wstring_view family) noexcept
    {
        for (std::size_t index = 0; index < std::size(kSubtitleFontFamilies); ++index)
        {
            if (kSubtitleFontFamilies[index] == family)
            {
                return index;
            }
        }
        // Anything unrecognised reads as the system face, which is also what the
        // playback side does with the alias "System" that it still accepts.
        return 1;
    }

    [[nodiscard]] inline std::size_t SubtitleOutlineIndex(std::wstring_view outline) noexcept
    {
        for (std::size_t index = 0; index < std::size(kSubtitleOutlines); ++index)
        {
            if (kSubtitleOutlines[index] == outline)
            {
                return index;
            }
        }
        // Normal, matching the settings service's own fallback.
        return 2;
    }

    // The preview caption is drawn at a fixed base size scaled by the same percentage mpv
    // applies through sub-scale, so the preview grows with the slider the way playback does.
    [[nodiscard]] inline double SubtitlePreviewFontSize(double scalePercent) noexcept
    {
        return 15.0 * scalePercent / 100.0;
    }

    // Border width follows the caption size the way libass scales its outline with the
    // font, so the preview keeps telling the truth as the size slider moves.
    [[nodiscard]] inline double SubtitlePreviewOutlineOffset(std::size_t outlineIndex, double scalePercent) noexcept
    {
        constexpr double widths[]{ 0.0, 0.8, 1.4, 2.2 };
        auto const index = outlineIndex < std::size(widths) ? outlineIndex : 2;
        return widths[index] * scalePercent / 100.0;
    }

    [[nodiscard]] inline double SubtitlePreviewShadowOffset(double scalePercent) noexcept
    {
        return 2.0 * scalePercent / 100.0;
    }
} // namespace HaloDesktop::ViewModels
