#include "Services/HistoryRowsPolicy.h"

#include "Services/ContinueShelfPolicy.h"
#include "Services/HomeRowsPolicy.h"
#include "Services/SearchResultsPolicy.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
    constexpr std::int64_t MillisecondsPerMinute = 60'000;
    constexpr std::int64_t MillisecondsPerDay = 24 * 60 * MillisecondsPerMinute;
    // The same line the continue shelf draws: at or past it nothing is left
    // worth returning to, whatever the watched flag says.
    constexpr double FinishedFraction = 0.95;

    constexpr std::array<wchar_t const*, 12> MonthNames{
        L"January", L"February", L"March", L"April", L"May", L"June",
        L"July", L"August", L"September", L"October", L"November", L"December" };
    constexpr std::array<wchar_t const*, 12> ShortMonthNames{
        L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
        L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };
    // Sunday first, as the day count below numbers them.
    constexpr std::array<wchar_t const*, 7> WeekdayNames{
        L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday" };

    // Floor division, so instants before 1970 land on the day they belong to.
    std::int64_t FloorDivide(std::int64_t value, std::int64_t divisor) noexcept
    {
        auto quotient = value / divisor;
        if ((value % divisor != 0) && ((value < 0) != (divisor < 0)))
        {
            --quotient;
        }
        return quotient;
    }

    // Days since 1970-01-01 in the proleptic Gregorian calendar.
    std::int64_t DaysFromCivil(std::int64_t year, std::int64_t month, std::int64_t day) noexcept
    {
        year -= month <= 2 ? 1 : 0;
        auto const era = FloorDivide(year, 400);
        auto const yearOfEra = year - era * 400;
        auto const dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        auto const dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
        return era * 146097 + dayOfEra - 719468;
    }

    ::HaloDesktop::Services::LocalDateTime CivilFromMilliseconds(std::int64_t milliseconds) noexcept
    {
        // Without multiplying back, which would overflow at the ends of the range.
        auto ofDay = milliseconds % MillisecondsPerDay;
        auto const days = milliseconds / MillisecondsPerDay - (ofDay < 0 ? 1 : 0);
        if (ofDay < 0) ofDay += MillisecondsPerDay;
        auto const shifted = days + 719468;
        auto const era = FloorDivide(shifted, 146097);
        auto const dayOfEra = shifted - era * 146097;
        auto const yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
        auto const dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
        auto const monthIndex = (5 * dayOfYear + 2) / 153;
        auto const day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
        auto const month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
        auto const year = yearOfEra + era * 400 + (month <= 2 ? 1 : 0);
        return {
            static_cast<std::int32_t>(year),
            static_cast<std::int32_t>(month),
            static_cast<std::int32_t>(day),
            static_cast<std::int32_t>(ofDay / (60 * MillisecondsPerMinute)),
            static_cast<std::int32_t>((ofDay / MillisecondsPerMinute) % 60),
        };
    }

    std::int64_t LocalDayNumber(
        std::int64_t timestamp,
        ::HaloDesktop::Services::LocalTimeOf const& localTime)
    {
        auto const local = localTime(timestamp);
        return DaysFromCivil(local.Year, local.Month, local.Day);
    }

    // Whole local calendar days from `timestamp` to `now`. Counting dates rather
    // than dividing milliseconds keeps a day across a daylight-saving change,
    // 23 or 25 hours long, one day.
    std::int64_t DaysAgo(
        std::int64_t timestamp,
        std::int64_t now,
        ::HaloDesktop::Services::LocalTimeOf const& localTime)
    {
        return LocalDayNumber(now, localTime) - LocalDayNumber(timestamp, localTime);
    }

    std::size_t MonthIndex(std::int32_t month) noexcept
    {
        return static_cast<std::size_t>(std::clamp(month, 1, 12) - 1);
    }

    std::wstring TwoDigits(std::int32_t value)
    {
        auto text = std::to_wstring(std::clamp(value, 0, 99));
        if (text.size() < 2) text.insert(text.begin(), L'0');
        return text;
    }

    std::wstring TimeOfDay(::HaloDesktop::Services::LocalDateTime const& local)
    {
        return TwoDigits(local.Hour) + L":" + TwoDigits(local.Minute);
    }

    wchar_t const* WeekdayOf(::HaloDesktop::Services::LocalDateTime const& local) noexcept
    {
        // 1970-01-01 was a Thursday.
        auto const days = DaysFromCivil(local.Year, local.Month, local.Day);
        auto const weekday = ((days % 7) + 7 + 4) % 7;
        return WeekdayNames[static_cast<std::size_t>(weekday)];
    }

    std::wstring GroupLabel(
        std::int64_t timestamp,
        std::int64_t today,
        ::HaloDesktop::Services::LocalTimeOf const& localTime)
    {
        auto const local = localTime(timestamp);
        auto const daysAgo = today - DaysFromCivil(local.Year, local.Month, local.Day);
        if (daysAgo <= 0) return L"Today";
        if (daysAgo == 1) return L"Yesterday";
        if (daysAgo < 7) return L"Earlier this week";
        return std::wstring{ MonthNames[MonthIndex(local.Month)] } + L" " + std::to_wstring(local.Year);
    }

    bool IsAsciiDigits(std::wstring_view text) noexcept
    {
        return !text.empty() && std::all_of(text.begin(), text.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
    }

    // A run of digits as JavaScript's `String(Number(part)).padStart(2, '0')`
    // prints it: leading zeros dropped, then at least two digits.
    std::wstring EpisodeNumber(std::wstring_view digits)
    {
        auto const first = digits.find_first_not_of(L'0');
        std::wstring text{ first == std::wstring_view::npos ? std::wstring_view{ L"0" } : digits.substr(first) };
        if (text.size() < 2) text.insert(text.begin(), 2 - text.size(), L'0');
        return text;
    }

    std::wstring AsciiUpper(std::wstring text)
    {
        for (auto& ch : text)
        {
            if (ch >= L'a' && ch <= L'z') ch = static_cast<wchar_t>(ch - L'a' + L'A');
        }
        return text;
    }
}

namespace HaloDesktop::Services
{
    LocalTimeOf FixedOffsetLocalTime(std::int32_t offsetMinutes)
    {
        return [offsetMinutes](std::int64_t epochMilliseconds)
        {
            auto const offset = static_cast<std::int64_t>(offsetMinutes) * MillisecondsPerMinute;
            // An instant at the very ends of the range keeps its UTC date rather
            // than overflowing.
            auto const limit = (std::numeric_limits<std::int64_t>::max)() - (std::max)(offset, -offset);
            auto const shifted = epochMilliseconds > -limit && epochMilliseconds < limit
                ? epochMilliseconds + offset
                : epochMilliseconds;
            return CivilFromMilliseconds(shifted);
        };
    }

    LocalTimeOf SystemLocalTime()
    {
        DYNAMIC_TIME_ZONE_INFORMATION zone{};
        auto const known = GetDynamicTimeZoneInformation(&zone) != TIME_ZONE_ID_INVALID;
        auto const universal = FixedOffsetLocalTime(0);
        return [zone, known, universal](std::int64_t epochMilliseconds)
        {
            // FILETIME counts 100ns ticks from 1601; anything it cannot hold, or
            // a zone Windows cannot read, is shown in universal time.
            constexpr std::int64_t FileTimeEpochOffset = 11'644'473'600'000;
            constexpr std::int64_t MaximumMilliseconds = (std::numeric_limits<std::int64_t>::max)() / 10'000 - FileTimeEpochOffset;
            if (known && epochMilliseconds >= -FileTimeEpochOffset && epochMilliseconds <= MaximumMilliseconds)
            {
                ULARGE_INTEGER ticks{};
                ticks.QuadPart = static_cast<std::uint64_t>(epochMilliseconds + FileTimeEpochOffset) * 10'000u;
                FILETIME fileTime{ ticks.LowPart, ticks.HighPart };
                SYSTEMTIME utc{};
                SYSTEMTIME local{};
                if (FileTimeToSystemTime(&fileTime, &utc) && SystemTimeToTzSpecificLocalTimeEx(&zone, &utc, &local))
                {
                    return LocalDateTime{ local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute };
                }
            }
            return universal(epochMilliseconds);
        };
    }

    std::vector<HistoryEntry> BuildHistory(
        std::span<Api::Dto::WatchEntry const> rows,
        std::span<Api::Dto::LibraryRow const> library,
        ::HaloDesktop::ViewModels::HomeFilter filter)
    {
        // As a JavaScript Map built from the list: a later entry for an id wins.
        std::unordered_map<std::wstring, Api::Dto::LibraryRow const*> libraryById;
        for (auto const& item : library)
        {
            if (!item.RemovedAt)
            {
                libraryById.insert_or_assign(std::wstring{ item.Id }, &item);
            }
        }

        std::vector<Api::Dto::WatchEntry const*> ordered;
        ordered.reserve(rows.size());
        for (auto const& row : rows)
        {
            ordered.push_back(&row);
        }
        std::stable_sort(ordered.begin(), ordered.end(), [](auto const* first, auto const* second)
        {
            return first->UpdatedAt > second->UpdatedAt;
        });

        std::vector<HistoryEntry> entries;
        entries.reserve(ordered.size());
        for (auto const* row : ordered)
        {
            // A tombstone is a forgotten video, never a play.
            if (row->RemovedAt)
            {
                continue;
            }
            std::wstring const itemId{ row->ItemId };
            auto const found = libraryById.find(itemId);
            auto const* saved = found == libraryById.end() ? nullptr : found->second;
            // `row.name ?? entry?.name`: a name the row carries wins even when empty.
            std::wstring name = row->Name
                ? std::wstring{ *row->Name }
                : (saved ? std::wstring{ saved->Name } : std::wstring{});
            auto type = TypeFromItemId(itemId);
            if (name.empty() || !MatchesHomeFilterType(filter, type))
            {
                continue;
            }
            std::optional<std::wstring> poster;
            if (row->Poster)
            {
                poster = std::wstring{ *row->Poster };
            }
            else if (saved && saved->Poster)
            {
                poster = std::wstring{ *saved->Poster };
            }
            auto const finished = row->Watched
                || (row->DurationSec > 0.0 && row->PositionSec / row->DurationSec >= FinishedFraction);
            entries.push_back({
                itemId,
                std::move(type),
                MetaIdFromItemId(itemId),
                std::wstring{ row->VideoId },
                std::move(name),
                std::move(poster),
                row->PositionSec,
                row->DurationSec,
                finished,
                row->UpdatedAt,
            });
        }
        return entries;
    }

    std::vector<HistoryGroup> GroupHistory(
        std::vector<HistoryEntry> entries,
        std::int64_t now,
        LocalTimeOf const& localTime)
    {
        std::vector<HistoryGroup> groups;
        if (!localTime)
        {
            return groups;
        }
        auto const today = LocalDayNumber(now, localTime);
        for (auto& entry : entries)
        {
            auto label = GroupLabel(entry.UpdatedAt, today, localTime);
            if (!groups.empty() && groups.back().Label == label)
            {
                groups.back().Entries.push_back(std::move(entry));
                continue;
            }
            HistoryGroup group;
            group.Label = std::move(label);
            group.Entries.push_back(std::move(entry));
            groups.push_back(std::move(group));
        }
        return groups;
    }

    std::wstring HistoryTime(std::int64_t timestamp, std::int64_t now, LocalTimeOf const& localTime)
    {
        if (!localTime)
        {
            return {};
        }
        auto const local = localTime(timestamp);
        auto const daysAgo = DaysAgo(timestamp, now, localTime);
        if (daysAgo <= 1) return TimeOfDay(local);
        if (daysAgo < 7) return WeekdayOf(local);
        return std::to_wstring(local.Day) + L" " + ShortMonthNames[MonthIndex(local.Month)];
    }

    std::wstring HistoryFullTime(std::int64_t timestamp, LocalTimeOf const& localTime)
    {
        if (!localTime)
        {
            return {};
        }
        auto const local = localTime(timestamp);
        return std::wstring{ WeekdayOf(local) } + L" " + std::to_wstring(local.Day) + L" "
            + MonthNames[MonthIndex(local.Month)] + L" " + std::to_wstring(local.Year) + L" at " + TimeOfDay(local);
    }

    std::vector<HistoryEntry> SearchHistory(std::vector<HistoryEntry> entries, std::wstring_view query)
    {
        auto const words = WhitespaceWords(FoldedText(query));
        if (words.empty())
        {
            return entries;
        }
        std::erase_if(entries, [&words](HistoryEntry const& entry)
        {
            auto const name = FoldedText(entry.Name);
            return !std::all_of(words.begin(), words.end(), [&name](std::wstring const& word)
            {
                return name.find(word) != std::wstring::npos;
            });
        });
        return entries;
    }

    std::wstring VideoIdTag(std::wstring_view videoId, std::wstring_view metaId)
    {
        std::wstring prefix{ metaId };
        prefix.push_back(L':');
        if (!videoId.starts_with(prefix))
        {
            return L"MOVIE";
        }
        auto const rest = videoId.substr(prefix.size());
        auto const separator = rest.find(L':');
        if (separator == std::wstring_view::npos)
        {
            return L"SERIES";
        }
        auto const season = rest.substr(0, separator);
        auto const episode = rest.substr(separator + 1);
        if (!IsAsciiDigits(season) || !IsAsciiDigits(episode))
        {
            return L"SERIES";
        }
        return L"S" + EpisodeNumber(season) + L"E" + EpisodeNumber(episode);
    }

    std::wstring FormatTimeLeft(double positionSec, double durationSec)
    {
        auto const remaining = (std::max)(0.0, durationSec - positionSec);
        if (!std::isfinite(remaining) || remaining < 60.0)
        {
            return L"under a min left";
        }
        // Math.round: halves go up.
        auto const minutes = static_cast<std::int64_t>(std::floor(remaining / 60.0 + 0.5));
        if (minutes < 60)
        {
            return std::to_wstring(minutes) + L" min left";
        }
        auto const hours = minutes / 60;
        auto const rest = minutes % 60;
        return rest == 0
            ? std::to_wstring(hours) + L" h left"
            : std::to_wstring(hours) + L" h " + std::to_wstring(rest) + L" min left";
    }

    std::wstring HistoryStateLabel(HistoryEntry const& entry)
    {
        if (entry.Finished) return L"WATCHED";
        if (entry.DurationSec > 0.0) return AsciiUpper(FormatTimeLeft(entry.PositionSec, entry.DurationSec));
        return L"STARTED";
    }

    std::wstring HistoryMetaLine(HistoryEntry const& entry)
    {
        return VideoIdTag(entry.VideoId, entry.MetaId) + L" · " + HistoryStateLabel(entry);
    }

    double HistoryProgress(HistoryEntry const& entry) noexcept
    {
        if (entry.Finished || !(entry.DurationSec > 0.0))
        {
            return 0.0;
        }
        auto const fraction = entry.PositionSec / entry.DurationSec;
        return std::isfinite(fraction) ? std::clamp(fraction, 0.0, 1.0) : 0.0;
    }

    std::optional<std::wstring> HistoryNote(
        HistoryLoadState state,
        bool hasAny,
        bool groupsEmpty,
        std::wstring_view query,
        ::HaloDesktop::ViewModels::HomeFilter filter)
    {
        if (state == HistoryLoadState::Loading) return std::wstring{ L"Loading your history…" };
        if (state == HistoryLoadState::Failed) return std::nullopt;
        if (!hasAny) return std::wstring{ L"Nothing watched yet. What you play shows up here." };
        if (!groupsEmpty) return std::nullopt;
        auto const trimmed = TrimmedText(query);
        if (!trimmed.empty())
        {
            return L"Nothing in your history matches “" + trimmed + L"”.";
        }
        // As the reference words it: anything but Movies reads as series.
        return filter == ::HaloDesktop::ViewModels::HomeFilter::Movies
            ? std::wstring{ L"No movies in your history." }
            : std::wstring{ L"No series in your history." };
    }

    HistoryView HistoryViewFromIndex(std::int32_t index) noexcept
    {
        switch (index)
        {
        case 1: return HistoryView::Movies;
        case 2: return HistoryView::Series;
        case 3: return HistoryView::InProgress;
        default: return HistoryView::All;
        }
    }

    namespace
    {
        bool ShowsIn(HistoryEntry const& entry, HistoryView view) noexcept
        {
            switch (view)
            {
            case HistoryView::Movies:
                return MatchesHomeFilterType(::HaloDesktop::ViewModels::HomeFilter::Movies, entry.Type);
            case HistoryView::Series:
                return MatchesHomeFilterType(::HaloDesktop::ViewModels::HomeFilter::Series, entry.Type);
            case HistoryView::InProgress:
                return !entry.Finished;
            case HistoryView::All:
                break;
            }
            return true;
        }
    }

    std::vector<HistoryEntry> FilterHistoryView(std::vector<HistoryEntry> entries, HistoryView view)
    {
        std::erase_if(entries, [view](HistoryEntry const& entry) { return !ShowsIn(entry, view); });
        return entries;
    }

    HistoryViewCounts CountHistoryViews(std::span<HistoryEntry const> entries)
    {
        HistoryViewCounts counts;
        for (auto const& entry : entries)
        {
            ++counts.All;
            if (ShowsIn(entry, HistoryView::Movies)) ++counts.Movies;
            if (ShowsIn(entry, HistoryView::Series)) ++counts.Series;
            if (ShowsIn(entry, HistoryView::InProgress)) ++counts.InProgress;
        }
        return counts;
    }

    std::optional<std::wstring> HistoryNote(
        HistoryLoadState state,
        bool hasAny,
        bool groupsEmpty,
        std::wstring_view query,
        HistoryView view)
    {
        auto const filter = view == HistoryView::Movies
            ? ::HaloDesktop::ViewModels::HomeFilter::Movies
            : view == HistoryView::Series
                ? ::HaloDesktop::ViewModels::HomeFilter::Series
                : ::HaloDesktop::ViewModels::HomeFilter::All;
        auto note = HistoryNote(state, hasAny, groupsEmpty, query, filter);
        if (view != HistoryView::InProgress || !note || state != HistoryLoadState::Loaded || !hasAny || !TrimmedText(query).empty())
        {
            return note;
        }
        return std::wstring{ L"Nothing in progress. Everything you started is finished." };
    }

    std::wstring HistoryStateText(HistoryEntry const& entry)
    {
        if (entry.Finished) return L"Watched";
        if (entry.DurationSec > 0.0) return FormatTimeLeft(entry.PositionSec, entry.DurationSec);
        return L"Started";
    }

    std::wstring HistoryTitleCount(std::size_t count)
    {
        return std::to_wstring(count) + (count == 1 ? L" title" : L" titles");
    }

    std::vector<std::wstring> ClearHistoryVideoIds(std::span<Api::Dto::WatchEntry const> rows)
    {
        std::vector<std::wstring> videoIds;
        std::unordered_set<std::wstring> seen;
        for (auto const& row : rows)
        {
            if (row.RemovedAt || row.VideoId.empty())
            {
                continue;
            }
            std::wstring videoId{ row.VideoId };
            if (seen.insert(videoId).second)
            {
                videoIds.push_back(std::move(videoId));
            }
        }
        return videoIds;
    }

    std::wstring ClearHistoryPrompt(std::size_t count)
    {
        return L"This forgets all " + std::to_wstring(count)
            + L" videos you have played: where you stopped, what you watched, and everything in Continue watching. It cannot be undone.";
    }
}
