#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::Controls
{
    // TMDB serves every image at named widths under `/t/p/<size>/`, and some
    // addons hand out the `original` upload: often 2000x3000 and several
    // megabytes, for a card that shows it a tenth of that size. Asking TMDB for
    // a sized copy fixes both the download and the downscale.
    inline constexpr int CardArtworkWidth = 500;

    // A TMDB `original` image as its [width]-pixel copy; any other URL,
    // including TMDB sizes already chosen, is returned as given.
    [[nodiscard]] std::wstring CardArtworkUrl(std::wstring_view url, int width = CardArtworkWidth);

    // Stremio's own poster for a bare IMDb id (`tt` and digits), which is what
    // an addon's catalog falls back on when its art does not load. Empty for
    // any other id, since metahub knows no other kind.
    [[nodiscard]] std::optional<std::wstring> MetahubPosterUrl(std::wstring_view id);

    // The sources to try in order, before the placeholder: the artwork, the
    // caller's own fallback, then metahub's poster for an IMDb id. Empty and
    // repeated entries are dropped.
    [[nodiscard]] std::vector<std::wstring> ArtworkCandidates(
        std::wstring_view source,
        std::wstring_view fallback,
        std::wstring_view metaId);

    // Whether a slot is small enough to be drawn from the card-sized copy. A
    // decode width of zero asks for full resolution (a backdrop), and anything
    // wider than the card copy would be upscaled from it.
    [[nodiscard]] bool UsesCardArtwork(double decodeWidth) noexcept;

    // How long to wait before trying every source again once all of them
    // failed, after `retriesDone` such rounds: 2, 8, then 30 seconds, then no
    // more. A brief network fault then mends itself on a page that stays open,
    // while artwork that is really missing costs three more rounds at most.
    [[nodiscard]] std::optional<std::chrono::milliseconds> ArtworkRetryDelay(int retriesDone) noexcept;
}
