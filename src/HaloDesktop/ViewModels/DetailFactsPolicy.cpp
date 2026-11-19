#include "ViewModels/DetailFactsPolicy.h"

#include "Services/StreamInfo.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <unordered_set>

namespace
{
    constexpr std::wstring_view UnknownTier = L"OTHER";
    constexpr std::size_t HeroGenreCount = 3;
    // Runtimes past this many minutes are clamped rather than overflowing; no
    // honest runtime comes near it.
    constexpr std::int64_t RuntimeCeiling = 1'000'000'000;

    constexpr std::wstring_view HexDigits = L"0123456789ABCDEF";

    constexpr std::array<wchar_t const*, 12> LongMonths{
        L"January", L"February", L"March", L"April", L"May", L"June",
        L"July", L"August", L"September", L"October", L"November", L"December",
    };
    constexpr std::array<wchar_t const*, 12> ShortMonths{
        L"JAN", L"FEB", L"MAR", L"APR", L"MAY", L"JUN", L"JUL", L"AUG", L"SEP", L"OCT", L"NOV", L"DEC",
    };

    // The source picker's tier labels (`streamInfo.ts` TIERS).
    std::wstring TierLabel(std::wstring_view quality)
    {
        if (quality == L"2160p") return L"4K";
        if (quality == L"1440p") return L"2K";
        if (quality == L"1080p") return L"FULL HD";
        if (quality == L"720p") return L"HD";
        if (quality == L"480p" || quality == L"SD") return L"SD";
        return std::wstring{ UnknownTier };
    }

    bool IsSpace(wchar_t character) noexcept
    {
        return std::iswspace(character) != 0;
    }

    bool IsAsciiDigit(wchar_t character) noexcept
    {
        return character >= L'0' && character <= L'9';
    }

    std::wstring_view Trimmed(std::wstring_view text) noexcept
    {
        while (!text.empty() && IsSpace(text.front())) text.remove_prefix(1);
        while (!text.empty() && IsSpace(text.back())) text.remove_suffix(1);
        return text;
    }

    std::wstring Lowered(std::wstring_view text)
    {
        std::wstring result;
        result.reserve(text.size());
        for (auto const character : text)
        {
            result.push_back(static_cast<wchar_t>(std::towlower(character)));
        }
        return result;
    }

    std::wstring Joined(std::vector<winrt::hstring> const& values, std::size_t limit)
    {
        std::wstring result;
        std::size_t taken{};
        for (auto const& value : values)
        {
            if (taken == limit) break;
            if (taken != 0) result.append(L", ");
            result.append(value);
            ++taken;
        }
        return result;
    }

    std::optional<std::wstring_view> View(std::optional<winrt::hstring> const& value) noexcept
    {
        return value ? std::optional<std::wstring_view>{ std::wstring_view{ *value } } : std::nullopt;
    }

    // The first run of digits followed, after optional white space, by `unit`,
    // which is what `/(\d+)\s*(?:h|hr|hour)/` and its minute twin find.
    std::optional<std::int64_t> NumberBefore(std::wstring_view text, wchar_t unit)
    {
        for (std::size_t index{}; index < text.size();)
        {
            if (!IsAsciiDigit(text[index]))
            {
                ++index;
                continue;
            }
            std::int64_t number{};
            while (index < text.size() && IsAsciiDigit(text[index]))
            {
                number = (std::min)(RuntimeCeiling, number * 10 + (text[index] - L'0'));
                ++index;
            }
            auto cursor = index;
            while (cursor < text.size() && IsSpace(text[cursor])) ++cursor;
            if (cursor < text.size() && text[cursor] == unit)
            {
                return number;
            }
        }
        return std::nullopt;
    }

    std::int64_t DaysFromCivil(std::int64_t year, std::int64_t month, std::int64_t day) noexcept
    {
        year -= month <= 2 ? 1 : 0;
        auto const era = (year >= 0 ? year : year - 399) / 400;
        auto const yearOfEra = year - era * 400;
        auto const dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        auto const dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
        return era * 146097 + dayOfEra - 719468;
    }

    HaloDesktop::ViewModels::CalendarDate CivilFromDays(std::int64_t days) noexcept
    {
        days += 719468;
        auto const era = (days >= 0 ? days : days - 146096) / 146097;
        auto const dayOfEra = days - era * 146097;
        auto const yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
        auto const dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
        auto const monthPrime = (5 * dayOfYear + 2) / 153;
        auto const day = dayOfYear - (153 * monthPrime + 2) / 5 + 1;
        auto const month = monthPrime < 10 ? monthPrime + 3 : monthPrime - 9;
        auto const year = yearOfEra + era * 400 + (month <= 2 ? 1 : 0);
        return { static_cast<std::int32_t>(year), static_cast<std::int32_t>(month), static_cast<std::int32_t>(day) };
    }

    bool IsLeapYear(std::int64_t year) noexcept
    {
        return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    }

    std::int32_t DaysInMonth(std::int64_t year, std::int32_t month) noexcept
    {
        constexpr std::array<std::int32_t, 12> days{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        if (month == 2 && IsLeapYear(year)) return 29;
        return days[static_cast<std::size_t>(month - 1)];
    }

    // Reads exactly `count` ASCII digits at `cursor`.
    std::optional<std::int32_t> Digits(std::wstring_view text, std::size_t& cursor, std::size_t count)
    {
        if (cursor + count > text.size()) return std::nullopt;
        std::int32_t value{};
        for (std::size_t index{}; index < count; ++index)
        {
            auto const character = text[cursor + index];
            if (!IsAsciiDigit(character)) return std::nullopt;
            value = value * 10 + (character - L'0');
        }
        cursor += count;
        return value;
    }

    bool Expect(std::wstring_view text, std::size_t& cursor, wchar_t character) noexcept
    {
        if (cursor >= text.size() || text[cursor] != character) return false;
        ++cursor;
        return true;
    }

    std::wstring TwoDigits(std::int32_t value)
    {
        auto text = std::to_wstring(value);
        if (text.size() < 2) text.insert(text.begin(), L'0');
        return text;
    }

    std::uint32_t CodePointAt(std::wstring_view text, std::size_t& index) noexcept
    {
        auto const first = static_cast<std::uint32_t>(text[index]);
        ++index;
        if (first >= 0xD800 && first <= 0xDBFF)
        {
            if (index < text.size())
            {
                auto const second = static_cast<std::uint32_t>(text[index]);
                if (second >= 0xDC00 && second <= 0xDFFF)
                {
                    ++index;
                    return 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
                }
            }
            return 0xFFFD;
        }
        if (first >= 0xDC00 && first <= 0xDFFF) return 0xFFFD;
        return first;
    }

    bool IsUnreserved(std::uint32_t point) noexcept
    {
        if ((point >= L'A' && point <= L'Z') || (point >= L'a' && point <= L'z') || (point >= L'0' && point <= L'9'))
        {
            return true;
        }
        constexpr std::wstring_view marks = L"-_.!~*'()";
        return point < 0x80 && marks.find(static_cast<wchar_t>(point)) != std::wstring_view::npos;
    }

    // `/^[A-Za-z0-9_-]{6,20}$/`.
    bool IsYouTubeId(std::wstring_view text) noexcept
    {
        if (text.size() < 6 || text.size() > 20) return false;
        return std::all_of(text.begin(), text.end(), [](wchar_t character)
        {
            return (character >= L'A' && character <= L'Z') || (character >= L'a' && character <= L'z')
                || IsAsciiDigit(character) || character == L'_' || character == L'-';
        });
    }

    bool IsWordSeparator(wchar_t character) noexcept
    {
        return IsSpace(character) || character == L'.' || character == L'_' || character == L'-';
    }
}

namespace HaloDesktop::ViewModels
{
    Availability SummarizeAvailability(std::span<Api::Dto::StreamRecord const> streams)
    {
        Availability result;
        std::unordered_set<std::wstring> languages;
        for (auto const& stream : streams)
        {
            auto const info = Services::ParseStreamInfo(stream);
            auto const& quality = info.Quality;
            auto const label = quality ? TierLabel(std::wstring_view{ *quality }) : std::wstring{ UnknownTier };
            auto const rank = Services::QualityRank(quality);
            auto const medal = MedalFor(quality ? std::wstring_view{ *quality } : NotParsedToken, L"SDR");
            auto const group = std::find_if(result.Groups.begin(), result.Groups.end(), [&label](QualityGroup const& item)
            {
                return item.Label == label;
            });
            if (group != result.Groups.end())
            {
                ++group->Count;
                if (rank < group->Rank)
                {
                    group->Rank = rank;
                    group->Medal = medal;
                }
            }
            else
            {
                result.Groups.push_back({ label, quality && *quality == L"2160p", medal, 1, rank });
            }
            for (auto const& subtitle : stream.Subtitles)
            {
                languages.insert(std::wstring{ subtitle.Lang });
            }
        }
        // Array.prototype.sort is stable, so equal ranks keep their first-seen order.
        std::stable_sort(result.Groups.begin(), result.Groups.end(), [](QualityGroup const& left, QualityGroup const& right)
        {
            return left.Rank < right.Rank;
        });
        result.Total = streams.size();
        result.SubtitleLanguages = languages.size();
        return result;
    }

    std::optional<std::int32_t> RuntimeMinutes(std::wstring_view runtime)
    {
        if (runtime.empty()) return std::nullopt;
        auto const text = Lowered(runtime);
        auto const hours = NumberBefore(text, L'h');
        auto const minutes = NumberBefore(text, L'm');
        if (hours || minutes)
        {
            auto const total = (std::min)(RuntimeCeiling, hours.value_or(0) * 60 + minutes.value_or(0));
            if (total <= 0) return std::nullopt;
            return static_cast<std::int32_t>(total);
        }
        // `/^\s*(\d{2,3})\s*$/`.
        auto const bare = Trimmed(text);
        if (bare.size() < 2 || bare.size() > 3 || !std::all_of(bare.begin(), bare.end(), IsAsciiDigit))
        {
            return std::nullopt;
        }
        std::int32_t value{};
        for (auto const character : bare) value = value * 10 + (character - L'0');
        return value;
    }

    std::optional<std::wstring> FormatRuntime(std::optional<std::wstring_view> runtime)
    {
        if (!runtime || runtime->empty()) return std::nullopt;
        auto const minutes = RuntimeMinutes(*runtime);
        if (!minutes || *minutes <= 0) return std::wstring{ *runtime };
        auto const hours = *minutes / 60;
        auto const rest = *minutes % 60;
        if (hours == 0) return std::to_wstring(rest) + L" min";
        return rest == 0
            ? std::to_wstring(hours) + L" h"
            : std::to_wstring(hours) + L" h " + std::to_wstring(rest) + L" min";
    }

    std::vector<Credit> CreditsOf(Api::Dto::MetaDetail const& meta)
    {
        std::vector<Credit> credits;
        if (!meta.PicturedCast.empty())
        {
            for (auto const& person : meta.PicturedCast)
            {
                Credit credit{ std::wstring{ person.Name }, std::nullopt, std::nullopt };
                if (person.Character && !person.Character->empty()) credit.Role = std::wstring{ *person.Character };
                if (person.Photo && !person.Photo->empty()) credit.Photo = std::wstring{ *person.Photo };
                credit.TmdbId = person.TmdbId;
                credits.push_back(std::move(credit));
            }
        }
        else
        {
            for (auto const& name : meta.Cast)
            {
                credits.push_back({ std::wstring{ name }, std::nullopt, std::nullopt });
            }
        }
        for (auto const& name : meta.Director)
        {
            credits.push_back({ std::wstring{ name }, std::wstring{ L"Director" }, std::nullopt });
        }

        std::unordered_set<std::wstring> seen;
        std::vector<Credit> result;
        result.reserve(credits.size());
        for (auto& credit : credits)
        {
            auto key = Lowered(Trimmed(credit.Name));
            if (key.empty() || !seen.insert(std::move(key)).second) continue;
            result.push_back(std::move(credit));
        }
        return result;
    }

    std::wstring EncodeUriComponent(std::wstring_view text)
    {
        std::wstring result;
        result.reserve(text.size());
        auto const escape = [&result](std::uint32_t byte)
        {
            result.push_back(L'%');
            result.push_back(HexDigits[(byte >> 4) & 0x0F]);
            result.push_back(HexDigits[byte & 0x0F]);
        };
        for (std::size_t index{}; index < text.size();)
        {
            auto const point = CodePointAt(text, index);
            if (IsUnreserved(point))
            {
                result.push_back(static_cast<wchar_t>(point));
                continue;
            }
            if (point < 0x80)
            {
                escape(point);
            }
            else if (point < 0x800)
            {
                escape(0xC0 | (point >> 6));
                escape(0x80 | (point & 0x3F));
            }
            else if (point < 0x10000)
            {
                escape(0xE0 | (point >> 12));
                escape(0x80 | ((point >> 6) & 0x3F));
                escape(0x80 | (point & 0x3F));
            }
            else
            {
                escape(0xF0 | (point >> 18));
                escape(0x80 | ((point >> 12) & 0x3F));
                escape(0x80 | ((point >> 6) & 0x3F));
                escape(0x80 | (point & 0x3F));
            }
        }
        return result;
    }

    std::wstring PersonSearchUrl(std::wstring_view name)
    {
        return L"https://www.imdb.com/find/?q=" + EncodeUriComponent(Trimmed(name)) + L"&s=nm";
    }

    std::optional<std::wstring> TrailerUrl(Api::Dto::MetaDetail const& meta)
    {
        std::vector<std::wstring_view> candidates;
        for (auto const& id : meta.TrailerStreamIds)
        {
            candidates.emplace_back(id);
        }
        for (auto const& trailer : meta.Trailers)
        {
            if (!trailer.Type || trailer.Type->empty() || Lowered(std::wstring_view{ *trailer.Type }) == L"trailer")
            {
                candidates.emplace_back(trailer.Source);
            }
        }
        auto const found = std::find_if(candidates.begin(), candidates.end(), IsYouTubeId);
        if (found == candidates.end()) return std::nullopt;
        return L"https://www.youtube.com/watch?v=" + std::wstring{ *found };
    }

    std::optional<CalendarDate> ParseIsoDate(std::wstring_view text)
    {
        std::size_t cursor{};
        auto const year = Digits(text, cursor, 4);
        if (!year) return std::nullopt;
        std::int32_t month = 1;
        std::int32_t day = 1;
        if (Expect(text, cursor, L'-'))
        {
            auto const parsedMonth = Digits(text, cursor, 2);
            if (!parsedMonth || *parsedMonth < 1 || *parsedMonth > 12) return std::nullopt;
            month = *parsedMonth;
            if (Expect(text, cursor, L'-'))
            {
                auto const parsedDay = Digits(text, cursor, 2);
                if (!parsedDay || *parsedDay < 1 || *parsedDay > DaysInMonth(*year, month)) return std::nullopt;
                day = *parsedDay;
            }
        }

        // The time only matters through its offset, which can move the UTC day.
        std::int64_t minuteOfDay{};
        if (cursor < text.size() && text[cursor] == L'T')
        {
            ++cursor;
            auto const hour = Digits(text, cursor, 2);
            if (!hour || !Expect(text, cursor, L':')) return std::nullopt;
            auto const minute = Digits(text, cursor, 2);
            if (!minute || *hour > 24 || *minute > 59) return std::nullopt;
            std::int32_t second{};
            bool fraction{};
            if (Expect(text, cursor, L':'))
            {
                auto const parsedSecond = Digits(text, cursor, 2);
                if (!parsedSecond || *parsedSecond > 59) return std::nullopt;
                second = *parsedSecond;
                if (Expect(text, cursor, L'.'))
                {
                    auto const start = cursor;
                    while (cursor < text.size() && IsAsciiDigit(text[cursor])) ++cursor;
                    if (cursor == start) return std::nullopt;
                    fraction = true;
                }
            }
            if (*hour == 24 && (*minute != 0 || second != 0 || fraction)) return std::nullopt;
            minuteOfDay = static_cast<std::int64_t>(*hour) * 60 + *minute;
            auto const utc = Expect(text, cursor, L'Z');
            if (!utc && cursor < text.size() && (text[cursor] == L'+' || text[cursor] == L'-'))
            {
                auto const sign = text[cursor] == L'-' ? -1 : 1;
                ++cursor;
                auto const offsetHours = Digits(text, cursor, 2);
                if (!offsetHours || !Expect(text, cursor, L':')) return std::nullopt;
                auto const offsetMinutes = Digits(text, cursor, 2);
                if (!offsetMinutes || *offsetHours > 23 || *offsetMinutes > 59) return std::nullopt;
                minuteOfDay -= sign * (static_cast<std::int64_t>(*offsetHours) * 60 + *offsetMinutes);
            }
        }
        if (cursor != text.size()) return std::nullopt;

        auto days = DaysFromCivil(*year, month, day);
        // Floor division: a negative minute of day belongs to the day before.
        days += minuteOfDay >= 0 ? minuteOfDay / 1440 : -((-minuteOfDay + 1439) / 1440);
        return CivilFromDays(days);
    }

    std::optional<std::wstring> FormatReleaseDate(std::optional<std::wstring_view> released)
    {
        if (!released || released->empty()) return std::nullopt;
        auto const date = ParseIsoDate(*released);
        if (!date) return std::nullopt;
        return std::to_wstring(date->Day) + L" " + LongMonths[static_cast<std::size_t>(date->Month - 1)] + L" "
            + std::to_wstring(date->Year);
    }

    std::vector<std::pair<std::wstring, std::wstring>> AboutRows(Api::Dto::MetaDetail const& meta)
    {
        std::vector<std::pair<std::wstring, std::optional<std::wstring>>> rows;
        rows.emplace_back(L"Director", meta.Director.empty() ? std::nullopt : std::optional<std::wstring>{ Joined(meta.Director, meta.Director.size()) });
        rows.emplace_back(L"Writers", meta.Writer.empty() ? std::nullopt : std::optional<std::wstring>{ Joined(meta.Writer, meta.Writer.size()) });
        rows.emplace_back(L"Released", FormatReleaseDate(View(meta.Released)));
        for (auto const& [label, value] : { std::pair{ L"Country", &meta.Country }, std::pair{ L"Language", &meta.Language }, std::pair{ L"Awards", &meta.Awards } })
        {
            rows.emplace_back(label, *value ? std::optional<std::wstring>{ std::wstring{ **value } } : std::nullopt);
        }

        std::vector<std::pair<std::wstring, std::wstring>> result;
        for (auto& [label, value] : rows)
        {
            if (!value || Trimmed(*value).empty()) continue;
            result.emplace_back(std::move(label), std::move(*value));
        }
        return result;
    }

    std::wstring FormatAirDate(std::optional<std::wstring_view> released)
    {
        if (!released || released->empty()) return {};
        auto const date = ParseIsoDate(*released);
        if (!date) return {};
        return std::wstring{ ShortMonths[static_cast<std::size_t>(date->Month - 1)] } + L" " + TwoDigits(date->Day) + L" "
            + std::to_wstring(date->Year);
    }

    std::wstring AirYear(std::optional<std::wstring_view> released)
    {
        if (!released || released->empty()) return {};
        auto const date = ParseIsoDate(*released);
        return date ? std::to_wstring(date->Year) : std::wstring{};
    }

    std::wstring Initials(std::wstring_view name)
    {
        std::vector<std::wstring_view> words;
        auto const trimmed = Trimmed(name);
        std::size_t start{};
        for (std::size_t index{}; index <= trimmed.size(); ++index)
        {
            if (index == trimmed.size() || IsWordSeparator(trimmed[index]))
            {
                if (index > start) words.push_back(trimmed.substr(start, index - start));
                start = index + 1;
            }
        }
        if (words.empty()) return L"?";
        std::wstring result;
        if (words.size() == 1)
        {
            result = std::wstring{ words.front().substr(0, 2) };
        }
        else
        {
            result.push_back(words[0].front());
            result.push_back(words[1].front());
        }
        for (auto& character : result)
        {
            character = static_cast<wchar_t>(std::towupper(character));
        }
        return result;
    }

    std::vector<std::wstring> HeroFacts(Api::Dto::MetaDetail const& meta, std::size_t numberedSeasons)
    {
        std::vector<std::wstring> facts;
        if (meta.Preview.ReleaseInfo && !meta.Preview.ReleaseInfo->empty())
        {
            facts.emplace_back(*meta.Preview.ReleaseInfo);
        }
        if (numberedSeasons > 0)
        {
            facts.push_back(CountOf(numberedSeasons, L"season", L"seasons"));
        }
        else if (auto runtime = FormatRuntime(View(meta.Runtime)))
        {
            facts.push_back(std::move(*runtime));
        }
        if (auto genres = Joined(meta.Preview.Genres, HeroGenreCount); !genres.empty())
        {
            facts.push_back(std::move(genres));
        }
        return facts;
    }

    std::wstring CountOf(std::size_t count, std::wstring_view singular, std::wstring_view plural)
    {
        return std::to_wstring(count) + L" " + std::wstring{ count == 1 ? singular : plural };
    }
}
