#include "HistoryRowsTests.h"

#include "Api/Dto.h"
#include "Services/HistoryRowsPolicy.h"
#include "Services/WatchMarkPolicy.h"
#include "Services/WatchStateService.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    using HaloDesktop::Api::Dto::LibraryRow;
    using HaloDesktop::Api::Dto::WatchEntry;
    using HaloDesktop::Services::BuildHistory;
    using HaloDesktop::Services::GroupHistory;
    using HaloDesktop::Services::HistoryEntry;
    using HaloDesktop::Services::HistoryLoadState;
    using HaloDesktop::Services::LocalDateTime;
    using HaloDesktop::Services::LocalTimeOf;
    using HaloDesktop::ViewModels::HomeFilter;

    constexpr std::int64_t Minute = 60'000;
    constexpr std::int64_t Hour = 60 * Minute;
    constexpr std::int64_t Day = 24 * Hour;

    // Days since 1970-01-01, for building instants from a wall clock.
    std::int64_t DaysFromCivil(std::int64_t year, std::int64_t month, std::int64_t day)
    {
        year -= month <= 2 ? 1 : 0;
        auto const era = (year >= 0 ? year : year - 399) / 400;
        auto const yearOfEra = year - era * 400;
        auto const dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        auto const dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
        return era * 146097 + dayOfEra - 719468;
    }

    // `new Date(year, month - 1, day, hour, minute)` in a zone `offsetMinutes`
    // east of UTC.
    std::int64_t LocalInstant(std::int32_t offsetMinutes, int year, int month, int day, int hour, int minute)
    {
        return DaysFromCivil(year, month, day) * Day + hour * Hour + minute * Minute - offsetMinutes * Minute;
    }

    // `row()` in the reference test: 600 of 3000 seconds, not watched, "Title".
    WatchEntry Row(std::wstring itemId, std::wstring videoId, std::int64_t updatedAt)
    {
        WatchEntry row;
        row.ItemId = winrt::hstring{ itemId };
        row.VideoId = winrt::hstring{ videoId };
        row.PositionSec = 600.0;
        row.DurationSec = 3000.0;
        row.Watched = false;
        row.Name = winrt::hstring{ L"Title" };
        row.UpdatedAt = updatedAt;
        return row;
    }

    WatchEntry Named(WatchEntry row, std::wstring_view name)
    {
        row.Name = winrt::hstring{ name };
        return row;
    }

    HistoryEntry EntryAt(std::int64_t updatedAt, int index)
    {
        HistoryEntry entry;
        entry.ItemId = L"movie:" + std::to_wstring(index);
        entry.Type = L"movie";
        entry.MetaId = std::to_wstring(index);
        entry.VideoId = std::to_wstring(index);
        entry.Name = L"x";
        entry.UpdatedAt = updatedAt;
        return entry;
    }

    void TestEveryPlayedVideoIsItsOwnRow()
    {
        auto watched = Named(Row(L"series:a", L"a:1:1", 10), L"Show");
        watched.Watched = true;
        auto film = Named(Row(L"movie:b", L"b", 20), L"Film");
        film.PositionSec = 2990.0;
        std::vector<WatchEntry> const rows{ watched, Named(Row(L"series:a", L"a:1:2", 30), L"Show"), film };
        auto const history = BuildHistory(rows, {}, HomeFilter::All);
        Require(history.size() == 3, "a played video was not its own row");
        Require(history[0].VideoId == L"a:1:2" && history[0].MetaId == L"a" && !history[0].Finished,
                "the newest row was not first, or its meta id or state was wrong");
        Require(history[1].VideoId == L"b" && history[1].MetaId == L"b" && history[1].Finished,
                "a video at 95 percent did not count as finished");
        Require(history[2].VideoId == L"a:1:1" && history[2].MetaId == L"a" && history[2].Finished,
                "a watched episode did not count as finished");
    }

    void TestFilterCountsNonSeriesAsMovies()
    {
        std::vector<WatchEntry> const rows{
            Row(L"series:a", L"a:1:1", 3), Row(L"movie:b", L"b", 2), Row(L"anime:kitsu:1", L"kitsu:1:1", 1) };
        auto const movies = BuildHistory(rows, {}, HomeFilter::Movies);
        Require(movies.size() == 2 && movies[0].ItemId == L"movie:b" && movies[1].ItemId == L"anime:kitsu:1",
                "a title that is not a series was not filed under movies");
        auto const series = BuildHistory(rows, {}, HomeFilter::Series);
        Require(series.size() == 1 && series[0].ItemId == L"series:a", "the series filter kept something else");
    }

    void TestLibraryOnlyLendsNameAndPoster()
    {
        LibraryRow saved;
        saved.Id = L"movie:b";
        saved.Type = L"movie";
        saved.Name = L"Saved name";
        saved.Poster = winrt::hstring{ L"https://p.test/b.jpg" };
        saved.AddedAt = 1;
        saved.UpdatedAt = 1;
        auto unnamedSaved = Row(L"movie:b", L"b", 2);
        unnamedSaved.Name.reset();
        auto unnamed = Row(L"movie:c", L"c", 1);
        unnamed.Name.reset();
        std::vector<LibraryRow> const library{ saved };
        std::vector<WatchEntry> const rows{ unnamedSaved, unnamed };
        auto const history = BuildHistory(rows, library, HomeFilter::All);
        Require(history.size() == 1 && history[0].Name == L"Saved name"
                    && history[0].Poster == std::optional<std::wstring>{ L"https://p.test/b.jpg" },
                "the library did not lend its name and poster, or an unnamed row stayed");

        // A title taken out of the library lends nothing.
        auto removed = saved;
        removed.RemovedAt = 5;
        std::vector<LibraryRow> const gone{ removed };
        Require(BuildHistory(rows, gone, HomeFilter::All).empty(), "a removed library entry lent its name");

        // `row.name ?? entry.name`: a name the row carries wins, even an empty one.
        std::vector<WatchEntry> const emptyNamed{ Named(Row(L"movie:b", L"b", 2), L"") };
        Require(BuildHistory(emptyNamed, library, HomeFilter::All).empty(),
                "an empty name on the row was replaced by the library's");

        // A removal tombstone is never a play.
        auto tombstone = Row(L"movie:d", L"d", 9);
        tombstone.RemovedAt = 9;
        std::vector<WatchEntry> const withTombstone{ tombstone };
        Require(BuildHistory(withTombstone, {}, HomeFilter::All).empty(), "a removed video was listed");
    }

    void RequireOctoberGroups(LocalTimeOf const& localTime, std::int32_t offsetMinutes, char const* message)
    {
        auto const now = LocalInstant(offsetMinutes, 2026, 10, 29, 15, 0);
        auto const at = [offsetMinutes](int day, int month)
        {
            return LocalInstant(offsetMinutes, 2026, month, day, 12, 0);
        };
        std::vector<std::int64_t> const stamps{ at(29, 10), at(29, 10), at(28, 10), at(24, 10), at(20, 10), at(2, 10), at(30, 9) };
        std::vector<HistoryEntry> entries;
        for (std::size_t index = 0; index < stamps.size(); ++index)
        {
            entries.push_back(EntryAt(stamps[index], static_cast<int>(index)));
        }
        auto const groups = GroupHistory(entries, now, localTime);
        Require(groups.size() == 5, message);
        Require(groups[0].Label == L"Today" && groups[0].Entries.size() == 2, message);
        Require(groups[1].Label == L"Yesterday" && groups[1].Entries.size() == 1, message);
        Require(groups[2].Label == L"Earlier this week" && groups[2].Entries.size() == 1, message);
        Require(groups[3].Label == L"October 2026" && groups[3].Entries.size() == 2, message);
        Require(groups[4].Label == L"September 2026" && groups[4].Entries.size() == 1, message);
    }

    void TestGroupsByLocalCalendar()
    {
        for (auto const offset : { 0, 330, -300, 840, -720 })
        {
            RequireOctoberGroups(HaloDesktop::Services::FixedOffsetLocalTime(offset), offset,
                                 "rows did not group into today, yesterday, this week and months by local calendar");
        }

        // The calendar is the viewer's, not UTC's: 20:00 UTC on the 28th is
        // already the 29th in India.
        auto const india = HaloDesktop::Services::FixedOffsetLocalTime(330);
        auto const now = LocalInstant(330, 2026, 10, 29, 9, 0);
        auto const lateUtc = DaysFromCivil(2026, 10, 28) * Day + 20 * Hour;
        auto const groups = GroupHistory({ EntryAt(lateUtc, 0) }, now, india);
        Require(groups.size() == 1 && groups[0].Label == L"Today", "a play was grouped by UTC rather than the local day");

        // Just past midnight, a play from just before it is yesterday's; a page
        // grouped again then must not keep calling it today.
        auto const utc = HaloDesktop::Services::FixedOffsetLocalTime(0);
        auto const lateEvening = LocalInstant(0, 2026, 10, 28, 23, 30);
        Require(GroupHistory({ EntryAt(lateEvening, 0) }, LocalInstant(0, 2026, 10, 28, 23, 50), utc)[0].Label == L"Today",
                "a play earlier the same evening was not today");
        Require(GroupHistory({ EntryAt(lateEvening, 0) }, LocalInstant(0, 2026, 10, 29, 0, 10), utc)[0].Label == L"Yesterday",
                "a play from before midnight stayed today after it");

        // A day across a daylight-saving change is 23 or 25 hours long and is
        // still one day. This zone moves from +1 to +2 at 01:00 UTC on 25 October.
        auto const changeAt = DaysFromCivil(2026, 10, 25) * Day + Hour;
        LocalTimeOf const summer = [changeAt](std::int64_t instant)
        {
            return HaloDesktop::Services::FixedOffsetLocalTime(instant < changeAt ? 60 : 120)(instant);
        };
        auto const dstNow = DaysFromCivil(2026, 10, 26) * Day + 22 * Hour + 30 * Minute; // 00:30 on the 27th locally
        auto const before = DaysFromCivil(2026, 10, 24) * Day + 22 * Hour + 30 * Minute; // 23:30 on the 24th locally
        auto const dst = GroupHistory({ EntryAt(dstNow - 10 * Minute, 0), EntryAt(before, 1) }, dstNow, summer);
        Require(dst.size() == 2 && dst[0].Label == L"Today" && dst[1].Label == L"Earlier this week",
                "a daylight-saving change moved a play into the wrong day");
        Require(HaloDesktop::Services::HistoryTime(before, dstNow, summer) == L"Saturday",
                "a daylight-saving change miscounted the days back");

        // A future stamp (a device whose clock ran ahead) reads as today.
        auto const future = GroupHistory({ EntryAt(now + 3 * Day, 0) }, now, india);
        Require(future.size() == 1 && future[0].Label == L"Today", "a future play was not today");
    }

    void TestHistoryTime()
    {
        auto const utc = HaloDesktop::Services::FixedOffsetLocalTime(0);
        auto const now = LocalInstant(0, 2026, 10, 29, 15, 0);
        Require(HaloDesktop::Services::HistoryTime(LocalInstant(0, 2026, 10, 29, 15, 4), now, utc) == L"15:04",
                "a play today did not print its time of day");
        Require(HaloDesktop::Services::HistoryTime(LocalInstant(0, 2026, 10, 28, 9, 5), now, utc) == L"09:05",
                "a play yesterday did not print its time of day");
        Require(HaloDesktop::Services::HistoryTime(LocalInstant(0, 2026, 10, 26, 12, 0), now, utc) == L"Monday",
                "a play this week did not print its weekday");
        Require(HaloDesktop::Services::HistoryTime(LocalInstant(0, 2026, 10, 23, 12, 0), now, utc) == L"Friday",
                "six days back did not print its weekday");
        Require(HaloDesktop::Services::HistoryTime(LocalInstant(0, 2026, 10, 22, 12, 0), now, utc) == L"22 Oct",
                "a week back did not print its date");
        Require(HaloDesktop::Services::HistoryTime(LocalInstant(0, 2025, 1, 3, 12, 0), now, utc) == L"3 Jan",
                "an old play did not print its date");
        Require(HaloDesktop::Services::HistoryFullTime(LocalInstant(0, 2026, 10, 29, 15, 4), utc) == L"Thursday 29 October 2026 at 15:04",
                "the tooltip's full date and time were wrong");
        // Before 1970 and at the ends of the range: still a date, never a crash.
        Require(HaloDesktop::Services::HistoryFullTime(-Day, utc) == L"Wednesday 31 December 1969 at 00:00",
                "an instant before 1970 was not dated");
        static_cast<void>(HaloDesktop::Services::HistoryTime((std::numeric_limits<std::int64_t>::min)(), now, utc));
        static_cast<void>(HaloDesktop::Services::HistoryFullTime((std::numeric_limits<std::int64_t>::max)(), utc));
        auto const system = HaloDesktop::Services::SystemLocalTime();
        static_cast<void>(HaloDesktop::Services::HistoryFullTime(now, system));
        static_cast<void>(HaloDesktop::Services::HistoryFullTime((std::numeric_limits<std::int64_t>::min)(), system));
    }

    void TestSearchHistory()
    {
        std::vector<WatchEntry> const rows{
            Named(Row(L"movie:a", L"a", 3), L"Spider-Man: Across the Spider-Verse"),
            Named(Row(L"movie:b", L"b", 2), L"Amélie"),
            Named(Row(L"movie:c", L"c", 1), L"Spider Baby") };
        auto const history = BuildHistory(rows, {}, HomeFilter::All);
        auto const verse = HaloDesktop::Services::SearchHistory(history, L"spider verse");
        Require(verse.size() == 1 && verse[0].VideoId == L"a", "search did not need every word");
        auto const amelie = HaloDesktop::Services::SearchHistory(history, L"AMELIE");
        Require(amelie.size() == 1 && amelie[0].VideoId == L"b", "search did not ignore case and accents");
        Require(HaloDesktop::Services::SearchHistory(history, L"   ").size() == 3, "a blank search filtered");
    }

    void TestRowText()
    {
        using HaloDesktop::Services::VideoIdTag;
        Require(VideoIdTag(L"tt1:2:4", L"tt1") == L"S02E04", "an episode id did not print its tag");
        Require(VideoIdTag(L"tt1:007:12", L"tt1") == L"S07E12", "leading zeros were not dropped from the tag");
        Require(VideoIdTag(L"tt1", L"tt1") == L"MOVIE", "a film's own id was not MOVIE");
        Require(VideoIdTag(L"kitsu:1:5", L"kitsu:1") == L"SERIES", "an id without season and episode was not SERIES");
        Require(VideoIdTag(L"tt1:1:x", L"tt1") == L"SERIES", "a non-numeric episode was not SERIES");
        Require(VideoIdTag(L"tt1:1:2:3", L"tt1") == L"SERIES", "an id of three parts was not SERIES");

        using HaloDesktop::Services::FormatTimeLeft;
        Require(FormatTimeLeft(0.0, 59.0) == L"under a min left", "under a minute was not said so");
        Require(FormatTimeLeft(0.0, 89.0) == L"1 min left" && FormatTimeLeft(0.0, 90.0) == L"2 min left",
                "minutes did not round as Math.round does");
        Require(FormatTimeLeft(600.0, 3480.0) == L"48 min left", "minutes left were wrong");
        Require(FormatTimeLeft(0.0, 7200.0) == L"2 h left", "whole hours left were wrong");
        Require(FormatTimeLeft(0.0, 3900.0) == L"1 h 5 min left", "hours and minutes left were wrong");
        Require(FormatTimeLeft(500.0, 100.0) == L"under a min left", "a position past the end was not under a minute");

        HistoryEntry partway;
        partway.MetaId = L"tt9";
        partway.VideoId = L"tt9";
        partway.PositionSec = 600.0;
        partway.DurationSec = 3480.0;
        Require(HaloDesktop::Services::HistoryMetaLine(partway) == L"MOVIE · 48 MIN LEFT", "a partway row's meta line was wrong");
        Require(std::abs(HaloDesktop::Services::HistoryProgress(partway) - 600.0 / 3480.0) < 1e-9, "a partway row's bar was wrong");
        auto finished = partway;
        finished.VideoId = L"tt9:1:2";
        finished.Finished = true;
        Require(HaloDesktop::Services::HistoryMetaLine(finished) == L"S01E02 · WATCHED", "a finished row's meta line was wrong");
        Require(HaloDesktop::Services::HistoryProgress(finished) == 0.0, "a finished row drew a bar");
        auto unknown = partway;
        unknown.DurationSec = 0.0;
        Require(HaloDesktop::Services::HistoryStateLabel(unknown) == L"STARTED", "a row of unknown length was not STARTED");
        Require(HaloDesktop::Services::HistoryProgress(unknown) == 0.0, "a row of unknown length drew a bar");
    }

    void TestNotes()
    {
        using HaloDesktop::Services::HistoryNote;
        Require(HistoryNote(HistoryLoadState::Loading, false, true, L"", HomeFilter::All) == std::optional<std::wstring>{ L"Loading your history…" },
                "the loading line was wrong");
        Require(!HistoryNote(HistoryLoadState::Failed, false, true, L"", HomeFilter::All), "a failed load drew a note of its own");
        Require(HistoryNote(HistoryLoadState::Loaded, false, true, L"", HomeFilter::All)
                    == std::optional<std::wstring>{ L"Nothing watched yet. What you play shows up here." },
                "the empty history line was wrong");
        Require(HistoryNote(HistoryLoadState::Loaded, true, true, L"  dune ", HomeFilter::Movies)
                    == std::optional<std::wstring>{ L"Nothing in your history matches “dune”." },
                "the search line was wrong or not trimmed");
        Require(HistoryNote(HistoryLoadState::Loaded, true, true, L"", HomeFilter::Movies)
                    == std::optional<std::wstring>{ L"No movies in your history." },
                "the movies line was wrong");
        Require(HistoryNote(HistoryLoadState::Loaded, true, true, L" ", HomeFilter::Series)
                    == std::optional<std::wstring>{ L"No series in your history." },
                "the series line was wrong");
        Require(!HistoryNote(HistoryLoadState::Loaded, true, false, L"dune", HomeFilter::All), "a note covered rows that matched");
        Require(HaloDesktop::Services::ClearHistoryPrompt(3)
                    == L"This forgets all 3 videos you have played: where you stopped, what you watched, and everything in Continue watching. It cannot be undone.",
                "the clear history question was wrong");
    }

    void TestRemovalSelection()
    {
        using HaloDesktop::Services::ChunkRows;
        using HaloDesktop::Services::ClearHistoryVideoIds;
        using HaloDesktop::Services::HistoryRowsOfVideos;

        auto gone = Row(L"movie:c", L"c", 3);
        gone.RemovedAt = 3;
        std::vector<WatchEntry> const rows{
            Row(L"series:a", L"a:1:1", 5), Row(L"movie:b", L"b", 4), gone, Row(L"series:a", L"a:1:2", 2) };

        // Remove from history: that one video's row and nothing else of the show.
        auto const one = HistoryRowsOfVideos(rows, { winrt::hstring{ L"a:1:1" } });
        Require(one.size() == 1 && one[0].VideoId == L"a:1:1", "removing one video took other rows with it");
        // Already removed, unknown or blank ids select nothing.
        Require(HistoryRowsOfVideos(rows, { winrt::hstring{ L"c" }, winrt::hstring{ L"zz" }, winrt::hstring{} }).empty(),
                "a removed, unknown or blank id selected a row");
        Require(HistoryRowsOfVideos(rows, {}).empty(), "an empty selection selected rows");

        // Clear history: every live row, each video once, tombstones left out.
        auto const shown = ClearHistoryVideoIds(rows);
        Require(shown == std::vector<std::wstring>{ L"a:1:1", L"b", L"a:1:2" }, "clear history did not ask about every live row");

        // Confirmed against the history as it is then: a row removed meanwhile
        // is not written again, and one that arrived meanwhile is not touched.
        std::vector<winrt::hstring> confirmed;
        for (auto const& videoId : shown) confirmed.emplace_back(videoId);
        auto meanwhile = rows;
        meanwhile[1].RemovedAt = 9;
        meanwhile.push_back(Row(L"movie:new", L"new", 10));
        auto const cleared = HistoryRowsOfVideos(meanwhile, confirmed);
        Require(cleared.size() == 2 && cleared[0].VideoId == L"a:1:1" && cleared[1].VideoId == L"a:1:2",
                "clear history wrote rows that were not live and shown when it was confirmed");

        // A long history goes out 500 rows a request, the last holding the rest.
        std::vector<WatchEntry> many;
        for (int index = 0; index < 1001; ++index)
        {
            many.push_back(Row(L"movie:m" + std::to_wstring(index), L"m" + std::to_wstring(index), index));
        }
        std::vector<winrt::hstring> all;
        for (auto const& videoId : ClearHistoryVideoIds(many)) all.emplace_back(videoId);
        auto const selected = HistoryRowsOfVideos(many, all);
        auto const tombstones = HaloDesktop::Services::RemovedRows(selected, 5000);
        auto const chunks = ChunkRows(tombstones, HaloDesktop::Services::WatchRemovalChunkSize);
        Require(selected.size() == 1001 && chunks.size() == 3 && chunks[0].size() == 500 && chunks[1].size() == 500
                    && chunks[2].size() == 1,
                "clearing a long history was not sent in batches of 500");
        Require(tombstones.front().RemovedAt && tombstones.back().RemovedAt && tombstones.back().UpdatedAt == 5000,
                "a cleared row was not stamped as removed");

        // A server that sends the removed rows back is too old, and says so in
        // the reference's words.
        Require(HaloDesktop::Services::EchoesRemovedRows(selected, tombstones), "an echoed clear was not caught");
        Require(std::string_view{ HaloDesktop::Services::HistoryRemovalUnsupported{}.what() }
                    == "Your Halo server cannot remove history yet. Nothing was lost; it needs updating first.",
                "the old-server message was not the reference's");
    }
}

void RunHistoryRowsTests()
{
    TestEveryPlayedVideoIsItsOwnRow();
    TestFilterCountsNonSeriesAsMovies();
    TestLibraryOnlyLendsNameAndPoster();
    TestGroupsByLocalCalendar();
    TestHistoryTime();
    TestSearchHistory();
    TestRowText();
    TestNotes();
    TestRemovalSelection();
}
