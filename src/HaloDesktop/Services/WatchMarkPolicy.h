#pragma once

#include "Api/Dto.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <winrt/base.h>

namespace HaloDesktop::Services
{
    // The video a mark is about, with the display fields its row carries. Name is
    // the show's name for an episode and the title for a film: what the continue
    // shelf prints.
    struct WatchMarkTarget final
    {
        winrt::hstring VideoId;
        winrt::hstring ItemId;
        winrt::hstring Name;
        winrt::hstring Poster;
    };

    // The row "Mark as watched" writes. A measured duration is kept; otherwise the
    // addon's runtime stands in, and with neither the duration stays 0, which every
    // reader takes as "length unknown". A length is never invented: the sources
    // sheet reads a row's duration as the video's real length for its estimates.
    [[nodiscard]] Api::Dto::WatchEntry WatchedRow(
        WatchMarkTarget const& target,
        std::optional<Api::Dto::WatchEntry> const& existing,
        double runtimeSeconds,
        std::int64_t now);

    // The row "Mark as unwatched" writes. Rows cannot be deleted (sync is last
    // write wins and the server keeps every row), so this is the row that reads as
    // never started everywhere: no position and no length, which the continue
    // shelf and the resume logic skip exactly like a missing row.
    [[nodiscard]] Api::Dto::WatchEntry UnwatchedRow(
        WatchMarkTarget const& target,
        std::optional<Api::Dto::WatchEntry> const& existing,
        std::int64_t now);

    // Rows marking several videos watched at once, given first to last. Each is
    // stamped strictly later than the one before it, so the continue shelf, which
    // follows a show's newest row, lands after the last of them rather than on
    // whichever the server lists first among equal timestamps.
    [[nodiscard]] std::vector<Api::Dto::WatchEntry> WatchedRowsInOrder(
        std::vector<WatchMarkTarget> const& targets,
        std::vector<Api::Dto::WatchEntry> const& existingRows,
        double runtimeSeconds,
        std::int64_t now);

    // Where an episode sits in its show. Season 0 holds the specials, and also
    // every episode of an addon that has no seasons at all.
    struct EpisodeSlot final
    {
        std::int32_t Season{};
        std::int32_t Number{};
    };

    // The positions in `episodes` of the ones before episodes[target], first to
    // last: every episode of an earlier season and the earlier ones of its own.
    // Specials are not part of that run, so they only count before another
    // special. Empty when target is out of range.
    [[nodiscard]] std::vector<std::size_t> EarlierEpisodes(
        std::vector<EpisodeSlot> const& episodes,
        std::size_t target);
}
