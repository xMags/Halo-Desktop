#pragma once

#include "Playback/IPlaybackEngine.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace HaloDesktop::ViewModels
{
    // The medal's metal: one per tier, so the finish alone says how good the picture
    // is. Gold for 4K, silver for Full HD (1440p and 1080p), bronze for HD, plain
    // below that.
    enum class MedalFinish
    {
        Gold,
        Silver,
        Bronze,
        Plain,
    };

    // A quality as a medal: the resolution on the dark plate, and a tag beside it
    // naming the dynamic range or, without one, the tier. Below HD there is no tag,
    // so the plate fills the badge. The phone and the reference desktop client stamp
    // the same three fields.
    struct Medal final
    {
        std::wstring Plate;
        // Empty when the medal has no tag.
        std::wstring Tag;
        MedalFinish Finish{ MedalFinish::Plain };

        bool operator==(Medal const&) const = default;
    };

    // The parser's placeholder for a quality or range it could not identify.
    inline constexpr std::wstring_view NotParsedToken = L"UNKNOWN";

    // A source's medal from its parsed quality token (2160p, 1440p, 1080p, 720p,
    // 480p, SD or UNKNOWN) and dynamic range token (SDR, DV, HDR10, HLG, HDR, or
    // empty). The reference's medalFor.
    [[nodiscard]] Medal MedalFor(std::wstring_view quality, std::wstring_view range);

    // The player's medal for the decoded picture, in the source picker's terms.
    // Classifies on pixel count so scope framing keeps its tier: a 2.39:1 film
    // mastered at 4K is 3840x1600, which a height rule would call 1080p. Dynamic
    // range replaces the tier's tag rather than joining it. Empty when there is no
    // picture to describe yet. The reference's classifyVideoQuality.
    [[nodiscard]] std::optional<Medal> MedalForPicture(Playback::VideoFormat const& format);
}
