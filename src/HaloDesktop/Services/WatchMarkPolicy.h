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

    // The tombstones that remove videos from the watch history, which forgets
    // where they stopped and whether they were watched. Sync is last write wins,
    // so a removal is a newer row rather than a deletion; servers keep it, never
    // send it back, and a later play writes a newer row that brings the video
    // back. The row otherwise stays as it was: a server too old to know RemovedAt
    // drops the field and stores the row, and must not then wipe the progress of
    // a video it goes on showing.
    //
    // RemovedAt and UpdatedAt are both `now`, or one past a row stamped later by
    // a device whose clock ran ahead, so every tombstone is strictly newer than
    // the row it replaces.
    [[nodiscard]] std::vector<Api::Dto::WatchEntry> RemovedRows(
        std::vector<Api::Dto::WatchEntry> const& rows,
        std::int64_t now);

    // Every row of the history that belongs to `itemId`, tombstones left out.
    // Removing a title from Continue watching forgets all of them: the shelf
    // shows a show by its latest episode, so forgetting that one alone would
    // only bring the one before it back.
    [[nodiscard]] std::vector<Api::Dto::WatchEntry> HistoryRowsOf(
        std::vector<Api::Dto::WatchEntry> const& rows,
        winrt::hstring const& itemId);

    // The rows a watch history read stands for: everything but removal
    // tombstones, as the reference client's `liveWatchStates`. A tombstone is
    // a forgotten video, never a play, a position or a watched mark.
    [[nodiscard]] std::vector<Api::Dto::WatchEntry> LiveWatchRows(
        std::vector<Api::Dto::WatchEntry> const& rows);

    // Consecutive runs of at most `size` rows (a size of 0 counts as 1), so a
    // long removal is not one huge request.
    [[nodiscard]] std::vector<std::vector<Api::Dto::WatchEntry>> ChunkRows(
        std::vector<Api::Dto::WatchEntry> const& rows,
        std::size_t size);

    // The batch size the history removal writes in, as the reference client does.
    inline constexpr std::size_t WatchRemovalChunkSize = 500;

    // True when the server answered a removal with one of the removed videos
    // still in the history: a server that does not know RemovedAt stores the row
    // and sends it straight back. A row that comes back as a tombstone is not one.
    [[nodiscard]] bool EchoesRemovedRows(
        std::vector<Api::Dto::WatchEntry> const& stored,
        std::vector<Api::Dto::WatchEntry> const& removed);

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
