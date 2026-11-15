#pragma once

#include "Api/Dto.h"
#include "ViewModels/QualityMedalPolicy.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace HaloDesktop::ViewModels
{
    // The reference client's `browse/detail/detailFacts.ts` and the display
    // formatters of its `format.ts` the title page uses. Pure: no XAML types,
    // so the test projects compile it by path.

    // One quality the addons offer, as the source picker names it.
    struct QualityGroup final
    {
        // The picker's tier label: `4K`, `2K`, `FULL HD`, `HD`, `SD`, or `OTHER`.
        std::wstring Label;
        // Gold is the picker's mark for the top tier, and only for it.
        bool Gold{};
        // The medal of the best quality in the group, as the picker stamps it.
        Medal Medal;
        std::size_t Count{};
        // Best-first ordering; lower is better.
        int Rank{};
    };

    struct Availability final
    {
        std::vector<QualityGroup> Groups;
        std::size_t Total{};
        std::size_t SubtitleLanguages{};
    };

    // What the addons offer for one video, grouped the way the picker shows it.
    // Tiers that share a label (480p and SD both read `SD`) count together, at
    // the better of their ranks.
    [[nodiscard]] Availability SummarizeAvailability(std::span<Api::Dto::StreamRecord const> streams);

    // Minutes from an addon's free-text runtime: `48 min`, `2 h 46 min`,
    // `2h46` or a bare two or three digit number. Anything else is empty rather
    // than a guess. The reference's `runtimeMinutes`.
    [[nodiscard]] std::optional<std::int32_t> RuntimeMinutes(std::wstring_view runtime);

    // `2 h 30 min` from an addon's `150 min`; the addon's own text when it is not minutes.
    [[nodiscard]] std::optional<std::wstring> FormatRuntime(std::optional<std::wstring_view> runtime);

    // A person on the cast row: the part they play, or their crew role.
    struct Credit final
    {
        std::wstring Name;
        std::optional<std::wstring> Role;
        std::optional<std::wstring> Photo;

        bool operator==(Credit const&) const = default;
    };

    // The cast row: the addon's pictured cast when it sends one, else the plain
    // names every addon has; the director(s) close the row either way. A name
    // appears once, at its first credit (trimmed, case-insensitive).
    [[nodiscard]] std::vector<Credit> CreditsOf(Api::Dto::MetaDetail const& meta);

    // JavaScript's encodeURIComponent: UTF-8, every byte percent-encoded except
    // the unreserved marks `A-Z a-z 0-9 - _ . ! ~ * ' ( )`. A lone surrogate is
    // encoded as U+FFFD rather than refused.
    [[nodiscard]] std::wstring EncodeUriComponent(std::wstring_view text);

    // Where a cast card leads: IMDb's people search for the trimmed name.
    [[nodiscard]] std::wstring PersonSearchUrl(std::wstring_view name);

    // The title's trailer on YouTube, from the addon's trailer streams or its
    // older trailer list (entries typed `trailer` or untyped); empty when it
    // sends none. Only an id in YouTube's own shape becomes a URL.
    [[nodiscard]] std::optional<std::wstring> TrailerUrl(Api::Dto::MetaDetail const& meta);

    struct CalendarDate final
    {
        std::int32_t Year{};
        std::int32_t Month{};
        std::int32_t Day{};

        bool operator==(CalendarDate const&) const = default;
    };

    // The UTC calendar date of an ISO 8601 date or date-time (`2025`, `2025-07`,
    // `2025-07-18`, `2025-07-18T23:30:00.000-05:00`). Empty when it is not one.
    [[nodiscard]] std::optional<CalendarDate> ParseIsoDate(std::wstring_view text);

    // `18 July 2025` (en-GB, UTC); empty when there is no date or it does not parse.
    [[nodiscard]] std::optional<std::wstring> FormatReleaseDate(std::optional<std::wstring_view> released);

    // The About panel's rows, in reading order; facts the addon does not send
    // (or sends as blank text) are left out.
    [[nodiscard]] std::vector<std::pair<std::wstring, std::wstring>> AboutRows(Api::Dto::MetaDetail const& meta);

    // `FEB 07 2026`: an episode's air date; empty when it does not parse.
    [[nodiscard]] std::wstring FormatAirDate(std::optional<std::wstring_view> released);

    // The four-digit year of an ISO date, for a season's summary line.
    [[nodiscard]] std::wstring AirYear(std::optional<std::wstring_view> released);

    // Up to two letters for a person without a photo.
    [[nodiscard]] std::wstring Initials(std::wstring_view name);

    // The hero's facts line before the age chip and the rating: the release,
    // the runtime ("N seasons" when the title has numbered seasons) and the
    // first three genres, leaving out whatever is missing.
    [[nodiscard]] std::vector<std::wstring> HeroFacts(Api::Dto::MetaDetail const& meta, std::size_t numberedSeasons);

    // "1 source", "3 sources": a count and its noun, singular only for one.
    [[nodiscard]] std::wstring CountOf(std::size_t count, std::wstring_view singular, std::wstring_view plural);
}
