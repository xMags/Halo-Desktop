#pragma once

#include "Api/Dto.h"
#include "ViewModels/HomeStatePolicy.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::Services
{
    // The History page's rows, ported from the reference client's
    // `history/historyRows.ts`: every video played, newest first, one row per
    // video (each episode of a show on its own line), read from the synced
    // watch history only. The saved library only lends a name or poster to
    // rows written before they carried their own. No UI or WinRT types in the
    // interface beyond the DTOs, so the tests compile this by path.

    struct HistoryEntry final
    {
        std::wstring ItemId;
        std::wstring Type;
        std::wstring MetaId;
        std::wstring VideoId;
        std::wstring Name;
        std::optional<std::wstring> Poster;
        double PositionSec{};
        double DurationSec{};
        // Watched, or so close to the end that nothing is left to return to.
        bool Finished{};
        // When it was last played, epoch milliseconds.
        std::int64_t UpdatedAt{};

        bool operator==(HistoryEntry const&) const = default;
    };

    struct HistoryGroup final
    {
        std::wstring Label;
        std::vector<HistoryEntry> Entries;

        bool operator==(HistoryGroup const&) const = default;
    };

    // A moment on the viewer's wall clock. Month is 1 to 12.
    struct LocalDateTime final
    {
        std::int32_t Year{};
        std::int32_t Month{};
        std::int32_t Day{};
        std::int32_t Hour{};
        std::int32_t Minute{};
    };

    // Turns an epoch-millisecond instant into the viewer's local time. Passed in
    // rather than read from the system so the calendar rules are deterministic
    // under test.
    using LocalTimeOf = std::function<LocalDateTime(std::int64_t epochMilliseconds)>;

    // The viewer's own time zone, with the daylight-saving rules Windows has for
    // the year of each instant. Taken when called, so a page that rebuilds asks
    // again and follows a time zone change.
    [[nodiscard]] LocalTimeOf SystemLocalTime();

    // Universal time at a fixed offset from it, in minutes east of UTC.
    [[nodiscard]] LocalTimeOf FixedOffsetLocalTime(std::int32_t offsetMinutes);

    // Watch rows as history entries, newest first. A row nothing names is left
    // out, and so is a removal tombstone. Anything that is not a series counts
    // as a movie for the filter.
    [[nodiscard]] std::vector<HistoryEntry> BuildHistory(
        std::span<Api::Dto::WatchEntry const> rows,
        std::span<Api::Dto::LibraryRow const> library,
        ::HaloDesktop::ViewModels::HomeFilter filter);

    // Splits newest-first entries into the page's sections by the viewer's local
    // calendar: Today, Yesterday, Earlier this week (fewer than seven days ago),
    // then one section per month ("October 2026"). A timestamp in the future
    // counts as today.
    [[nodiscard]] std::vector<HistoryGroup> GroupHistory(
        std::vector<HistoryEntry> entries,
        std::int64_t now,
        LocalTimeOf const& localTime);

    // When an entry was played, as its row prints it: the time of day within the
    // last two days ("15:04"), the weekday within the week ("Thursday"), the date
    // before that ("29 Oct").
    [[nodiscard]] std::wstring HistoryTime(
        std::int64_t timestamp,
        std::int64_t now,
        LocalTimeOf const& localTime);

    // The full date and time, for the row's tooltip:
    // "Thursday 29 October 2026 at 15:04".
    [[nodiscard]] std::wstring HistoryFullTime(std::int64_t timestamp, LocalTimeOf const& localTime);

    // The entries whose title contains every word of `query`, ignoring case and
    // accents, so "spider verse" finds "Spider-Man: Across the Spider-Verse". A
    // query of only spaces keeps everything.
    [[nodiscard]] std::vector<HistoryEntry> SearchHistory(
        std::vector<HistoryEntry> entries,
        std::wstring_view query);

    // The tag a watch row can print without its title's details: MOVIE when the
    // video is the title itself, S02E04 when the id carries `:season:episode`
    // after the meta id, and SERIES for any other episode id.
    [[nodiscard]] std::wstring VideoIdTag(std::wstring_view videoId, std::wstring_view metaId);

    // "under a min left", "48 min left", "2 h left", "1 h 5 min left".
    [[nodiscard]] std::wstring FormatTimeLeft(double positionSec, double durationSec);

    // What the row says after its tag: WATCHED, the time left in capitals, or
    // STARTED when the video's length is unknown.
    [[nodiscard]] std::wstring HistoryStateLabel(HistoryEntry const& entry);

    // "{tag} · {state}".
    [[nodiscard]] std::wstring HistoryMetaLine(HistoryEntry const& entry);

    // How far through the video the row's bar is filled, 0 to 1. Empty for a
    // finished video and one of unknown length.
    [[nodiscard]] double HistoryProgress(HistoryEntry const& entry) noexcept;

    // Where the history stands, as the page's note reads it.
    enum class HistoryLoadState
    {
        Loading,
        Failed,
        Loaded,
    };

    // The line the page shows instead of, or above, its sections: loading,
    // nothing watched, nothing matching the search, nothing of the chosen kind.
    // Empty when the sections speak for themselves or the load failed (the page
    // says that on its own). `hasAny` counts every live row, named or not.
    [[nodiscard]] std::optional<std::wstring> HistoryNote(
        HistoryLoadState state,
        bool hasAny,
        bool groupsEmpty,
        std::wstring_view query,
        ::HaloDesktop::ViewModels::HomeFilter filter);

    // What "Clear history" forgets: every live row, whatever the filter and the
    // search show, as the dialog that asks first says. Each video once.
    [[nodiscard]] std::vector<std::wstring> ClearHistoryVideoIds(std::span<Api::Dto::WatchEntry const> rows);

    // "Clear your watch history?" dialog's body for `count` videos.
    [[nodiscard]] std::wstring ClearHistoryPrompt(std::size_t count);
}
