#pragma once

#include <string>
#include <string_view>

namespace HaloDesktop::Services
{
    // The reference client's `browse/playableVideo.ts`.

    // The video id a film's sources, progress and download are looked up by:
    // the addon's `defaultVideoId` when it names one, else the title's own id.
    // The TMDB addon lists films under its own `tmdb:` ids, which stream addons
    // do not know, and names the IMDb id to play instead, as Stremio honours.
    [[nodiscard]] std::wstring MovieVideoId(std::wstring_view id, std::wstring_view defaultVideoId);

    // Whether a film can be played from a catalog card as it is: its id is
    // already the one stream addons look up (IMDb), or the card names the
    // video. Otherwise the title page, which loads the full details, is the way in.
    [[nodiscard]] bool PlayableFromPreview(std::wstring_view id, std::wstring_view defaultVideoId);
}
