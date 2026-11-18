#include "pch.h"
#include "Api/Addons/AddonClientPolicy.h"

#include <algorithm>
#include <array>
#include <numeric>

namespace
{
    bool IsAsciiAlpha(wchar_t character) noexcept
    {
        return (character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z');
    }

    bool IsAsciiDigit(wchar_t character) noexcept
    {
        return character >= L'0' && character <= L'9';
    }

    bool IsHexDigit(wchar_t character) noexcept
    {
        return IsAsciiDigit(character)
            || (character >= L'a' && character <= L'f')
            || (character >= L'A' && character <= L'F');
    }

    wchar_t AsciiLower(wchar_t character) noexcept
    {
        return character >= L'A' && character <= L'Z'
            ? static_cast<wchar_t>(character - L'A' + L'a')
            : character;
    }

    std::wstring AsciiLowered(std::wstring_view text)
    {
        std::wstring result{ text };
        std::transform(result.begin(), result.end(), result.begin(), AsciiLower);
        return result;
    }

    bool IsSpecialScheme(std::wstring_view scheme) noexcept
    {
        return scheme == L"http" || scheme == L"https" || scheme == L"ws"
            || scheme == L"wss" || scheme == L"ftp";
    }

    // The WHATWG forbidden domain code points that can appear in a host
    // written in a URL (percent-decoding and IDNA are not modelled: a host
    // that needs either is refused, which only ever refuses an odd URL).
    bool IsForbiddenHostCharacter(wchar_t character) noexcept
    {
        if (character <= 0x20 || character == 0x7F) return true;
        constexpr std::wstring_view forbidden = L"#%/:<>?@[\\]^|";
        return forbidden.find(character) != std::wstring_view::npos;
    }

    bool ValidPort(std::wstring_view port) noexcept
    {
        if (port.empty()) return true;
        std::uint32_t value{};
        for (auto const character : port)
        {
            if (!IsAsciiDigit(character)) return false;
            value = value * 10u + static_cast<std::uint32_t>(character - L'0');
            if (value > 65535u) return false;
        }
        return true;
    }

    bool StartsWith(std::wstring_view text, std::wstring_view prefix) noexcept
    {
        return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
    }

    bool Contains(std::vector<std::wstring> const& values, std::wstring_view value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    bool MatchesPrefixes(std::optional<std::vector<std::wstring>> const& prefixes, std::wstring_view id)
    {
        if (id.empty() || !prefixes) return true;
        return std::any_of(prefixes->begin(), prefixes->end(), [id](std::wstring const& prefix)
        {
            return StartsWith(id, prefix);
        });
    }

    double SeasonKey(double season) noexcept
    {
        // Specials sort after every regular season, Stremio's convention.
        return season == 0 ? 9007199254740991.0 : season;
    }

    // Days from 1970-01-01 to the civil date (proleptic Gregorian).
    std::int64_t DaysFromCivil(std::int64_t year, std::int64_t month, std::int64_t day) noexcept
    {
        year -= month <= 2 ? 1 : 0;
        auto const era = (year >= 0 ? year : year - 399) / 400;
        auto const yearOfEra = year - era * 400;
        auto const dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        auto const dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
        return era * 146097 + dayOfEra - 719468;
    }

    bool IsLeapYear(std::int64_t year) noexcept
    {
        return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    }

    std::int64_t DaysInMonth(std::int64_t year, std::int64_t month) noexcept
    {
        constexpr std::array<std::int64_t, 12> days{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        return month == 2 && IsLeapYear(year) ? 29 : days[static_cast<std::size_t>(month - 1)];
    }

    // Reads exactly `count` digits at `position`.
    std::optional<std::int64_t> Digits(std::wstring_view text, std::size_t& position, std::size_t count) noexcept
    {
        if (position + count > text.size()) return std::nullopt;
        std::int64_t value{};
        for (std::size_t index = 0; index < count; ++index)
        {
            auto const character = text[position + index];
            if (!IsAsciiDigit(character)) return std::nullopt;
            value = value * 10 + (character - L'0');
        }
        position += count;
        return value;
    }
}

namespace HaloDesktop::Api::Addons
{
    bool IsJsWhitespace(wchar_t character) noexcept
    {
        switch (character)
        {
        case 0x0009: case 0x000A: case 0x000B: case 0x000C: case 0x000D:
        case 0x0020: case 0x00A0: case 0x1680: case 0x2028: case 0x2029:
        case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
            return true;
        default:
            return character >= 0x2000 && character <= 0x200A;
        }
    }

    std::wstring JsTrim(std::wstring_view text)
    {
        std::size_t first{};
        auto last = text.size();
        while (first < last && IsJsWhitespace(text[first])) ++first;
        while (last > first && IsJsWhitespace(text[last - 1])) --last;
        return std::wstring{ text.substr(first, last - first) };
    }

    std::optional<UrlFacts> ParseUrl(std::wstring_view value)
    {
        // Leading and trailing C0 controls and spaces are stripped, and tabs
        // and newlines anywhere are removed, before anything is read.
        std::size_t first{};
        auto last = value.size();
        while (first < last && value[first] <= 0x20) ++first;
        while (last > first && value[last - 1] <= 0x20) --last;
        std::wstring input;
        input.reserve(last - first);
        for (auto const character : value.substr(first, last - first))
        {
            if (character != L'\t' && character != L'\n' && character != L'\r') input.push_back(character);
        }

        if (input.empty() || !IsAsciiAlpha(input.front())) return std::nullopt;
        std::size_t schemeEnd = 1;
        while (schemeEnd < input.size()
            && (IsAsciiAlpha(input[schemeEnd]) || IsAsciiDigit(input[schemeEnd])
                || input[schemeEnd] == L'+' || input[schemeEnd] == L'-' || input[schemeEnd] == L'.'))
        {
            ++schemeEnd;
        }
        if (schemeEnd >= input.size() || input[schemeEnd] != L':') return std::nullopt;

        UrlFacts facts{ AsciiLowered(std::wstring_view{ input }.substr(0, schemeEnd)), {} };
        if (!IsSpecialScheme(facts.Scheme))
        {
            // file: and the opaque schemes (mailto:, magnet:, javascript:)
            // parse without a host.
            return facts;
        }

        std::wstring_view const rest = std::wstring_view{ input }.substr(schemeEnd + 1);
        std::size_t authorityStart{};
        while (authorityStart < rest.size() && (rest[authorityStart] == L'/' || rest[authorityStart] == L'\\'))
        {
            ++authorityStart;
        }
        auto authorityEnd = rest.find_first_of(L"/\\?#", authorityStart);
        if (authorityEnd == std::wstring_view::npos) authorityEnd = rest.size();
        auto authority = rest.substr(authorityStart, authorityEnd - authorityStart);
        if (auto const at = authority.rfind(L'@'); at != std::wstring_view::npos)
        {
            authority = authority.substr(at + 1);
        }

        std::wstring_view host;
        std::wstring_view port;
        if (!authority.empty() && authority.front() == L'[')
        {
            auto const close = authority.find(L']');
            if (close == std::wstring_view::npos) return std::nullopt;
            host = authority.substr(0, close + 1);
            auto const after = authority.substr(close + 1);
            if (!after.empty())
            {
                if (after.front() != L':') return std::nullopt;
                port = after.substr(1);
            }
            for (auto const character : host.substr(1, host.size() - 2))
            {
                if (!IsHexDigit(character) && character != L':' && character != L'.') return std::nullopt;
            }
        }
        else
        {
            auto const colon = authority.find(L':');
            host = authority.substr(0, colon);
            if (colon != std::wstring_view::npos) port = authority.substr(colon + 1);
            if (std::any_of(host.begin(), host.end(), IsForbiddenHostCharacter)) return std::nullopt;
        }
        if (host.empty() || !ValidPort(port)) return std::nullopt;
        facts.Host = AsciiLowered(host);
        return facts;
    }

    bool IsUrl(std::wstring_view value)
    {
        return ParseUrl(value).has_value();
    }

    bool IsHttpUrl(std::wstring_view value)
    {
        auto const facts = ParseUrl(value);
        return facts && (facts->Scheme == L"http" || facts->Scheme == L"https") && !facts->Host.empty();
    }

    bool IsHttpsUrl(std::wstring_view value)
    {
        auto const facts = ParseUrl(value);
        return facts && facts->Scheme == L"https" && !facts->Host.empty();
    }

    std::wstring TransportBase(std::wstring_view transportUrl)
    {
        constexpr std::wstring_view manifest = L"/manifest.json";
        if (transportUrl.size() >= manifest.size()
            && transportUrl.substr(transportUrl.size() - manifest.size()) == manifest)
        {
            transportUrl.remove_suffix(manifest.size());
        }
        if (!transportUrl.empty() && transportUrl.back() == L'/')
        {
            transportUrl.remove_suffix(1);
        }
        return std::wstring{ transportUrl };
    }

    std::wstring ManifestUrl(std::wstring_view transportUrl)
    {
        return TransportBase(transportUrl) + L"/manifest.json";
    }

    std::optional<std::wstring> EncodeUriComponent(std::wstring_view text)
    {
        constexpr wchar_t hex[] = L"0123456789ABCDEF";
        std::wstring result;
        result.reserve(text.size());
        auto const appendByte = [&result, &hex](std::uint32_t byte)
        {
            auto const unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
                || (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.'
                || byte == '!' || byte == '~' || byte == '*' || byte == '\'' || byte == '(' || byte == ')';
            if (unreserved)
            {
                result.push_back(static_cast<wchar_t>(byte));
                return;
            }
            result.push_back(L'%');
            result.push_back(hex[(byte >> 4) & 0x0F]);
            result.push_back(hex[byte & 0x0F]);
        };

        for (std::size_t index = 0; index < text.size(); ++index)
        {
            std::uint32_t codePoint = text[index];
            if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
            {
                if (index + 1 >= text.size()) return std::nullopt;
                std::uint32_t const low = text[index + 1];
                if (low < 0xDC00 || low > 0xDFFF) return std::nullopt;
                codePoint = 0x10000u + ((codePoint - 0xD800u) << 10) + (low - 0xDC00u);
                ++index;
            }
            else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
            {
                return std::nullopt;
            }

            if (codePoint < 0x80)
            {
                appendByte(codePoint);
            }
            else if (codePoint < 0x800)
            {
                appendByte(0xC0u | (codePoint >> 6));
                appendByte(0x80u | (codePoint & 0x3Fu));
            }
            else if (codePoint < 0x10000)
            {
                appendByte(0xE0u | (codePoint >> 12));
                appendByte(0x80u | ((codePoint >> 6) & 0x3Fu));
                appendByte(0x80u | (codePoint & 0x3Fu));
            }
            else
            {
                appendByte(0xF0u | (codePoint >> 18));
                appendByte(0x80u | ((codePoint >> 12) & 0x3Fu));
                appendByte(0x80u | ((codePoint >> 6) & 0x3Fu));
                appendByte(0x80u | (codePoint & 0x3Fu));
            }
        }
        return result;
    }

    std::optional<std::wstring> EncodeExtra(ExtraArgs const& extra)
    {
        std::wstring result;
        for (auto const& [name, value] : extra)
        {
            auto const encodedName = EncodeUriComponent(name);
            auto const encodedValue = EncodeUriComponent(value);
            if (!encodedName || !encodedValue) return std::nullopt;
            if (!result.empty()) result.push_back(L'&');
            result.append(*encodedName).append(L"=").append(*encodedValue);
        }
        return result;
    }

    std::wstring_view ResourceName(AddonResource resource) noexcept
    {
        switch (resource)
        {
        case AddonResource::Catalog: return L"catalog";
        case AddonResource::Meta: return L"meta";
        case AddonResource::Stream: return L"stream";
        case AddonResource::Subtitles: return L"subtitles";
        }
        return L"";
    }

    std::optional<std::wstring> ResourceUrl(
        std::wstring_view transportUrl,
        AddonResource resource,
        std::wstring_view type,
        std::wstring_view id,
        ExtraArgs const& extra)
    {
        auto const encodedType = EncodeUriComponent(type);
        auto const encodedId = EncodeUriComponent(id);
        if (!encodedType || !encodedId) return std::nullopt;
        auto url = TransportBase(transportUrl);
        url.append(L"/").append(ResourceName(resource)).append(L"/").append(*encodedType)
            .append(L"/").append(*encodedId);
        if (!extra.empty())
        {
            auto const encodedExtra = EncodeExtra(extra);
            if (!encodedExtra) return std::nullopt;
            url.append(L"/").append(*encodedExtra);
        }
        url.append(L".json");
        return url;
    }

    bool AddonSupportsResource(
        ManifestFacts const& manifest,
        AddonResource resource,
        std::wstring_view type,
        std::wstring_view id)
    {
        auto const name = ResourceName(resource);
        for (auto const& entry : manifest.Resources)
        {
            if (entry.Name != name) continue;
            if (entry.Bare)
            {
                if (Contains(manifest.Types, type) && MatchesPrefixes(manifest.IdPrefixes, id)) return true;
            }
            else if (Contains(entry.Types, type)
                && MatchesPrefixes(entry.IdPrefixes ? entry.IdPrefixes : manifest.IdPrefixes, id))
            {
                return true;
            }
        }
        return false;
    }

    ExtraArgs CatalogExtras(ExtraArgs const& requested)
    {
        ExtraArgs result;
        for (auto const& [name, value] : requested)
        {
            if (name == L"addon" || name == L"type" || name == L"id") continue;
            auto const seen = std::any_of(result.begin(), result.end(), [&name](auto const& entry)
            {
                return entry.first == name;
            });
            if (!seen) result.emplace_back(name, value);
        }
        return result;
    }

    std::optional<std::wstring> CatalogExtraProblem(ExtraArgs const& extra)
    {
        constexpr std::size_t MaximumExtras = 8;
        constexpr std::size_t MaximumKeyLength = 64;
        constexpr std::size_t MaximumValueLength = 256;
        if (extra.size() > MaximumExtras) return L"too many extra params";
        auto const tooLong = std::any_of(extra.begin(), extra.end(), [](auto const& entry)
        {
            return entry.first.size() > MaximumKeyLength || entry.second.size() > MaximumValueLength;
        });
        if (tooLong) return L"extra param too long";
        return std::nullopt;
    }

    std::optional<std::wstring> SubtitleQueryProblem(SubtitleQuery const& query)
    {
        constexpr std::size_t MaximumFilenameLength = 1024;
        if (query.VideoHash)
        {
            auto const& hash = *query.VideoHash;
            if (hash.size() != 16 || !std::all_of(hash.begin(), hash.end(), IsHexDigit))
            {
                return L"videoHash must be 16 hex chars";
            }
        }
        if (query.VideoSize && *query.VideoSize == 0) return L"videoSize must be a positive integer";
        if (query.Filename && query.Filename->size() > MaximumFilenameLength) return L"filename too long";
        return std::nullopt;
    }

    ExtraArgs SubtitleExtras(SubtitleQuery const& query)
    {
        ExtraArgs result;
        if (query.VideoHash && !query.VideoHash->empty()) result.emplace_back(L"videoHash", *query.VideoHash);
        if (query.VideoSize && *query.VideoSize != 0) result.emplace_back(L"videoSize", std::to_wstring(*query.VideoSize));
        if (query.Filename && !query.Filename->empty()) result.emplace_back(L"filename", *query.Filename);
        return result;
    }

    std::optional<std::size_t> NextVideoIndex(
        std::vector<VideoOrderKey> const& videos,
        std::wstring_view currentVideoId,
        std::int64_t nowMs)
    {
        std::vector<std::size_t> order(videos.size());
        std::iota(order.begin(), order.end(), std::size_t{ 0 });
        auto const numbered = std::all_of(videos.begin(), videos.end(), [](VideoOrderKey const& video)
        {
            return video.Season.has_value() && video.Episode.has_value();
        });
        if (numbered)
        {
            std::stable_sort(order.begin(), order.end(), [&videos](std::size_t left, std::size_t right)
            {
                auto const leftSeason = SeasonKey(*videos[left].Season);
                auto const rightSeason = SeasonKey(*videos[right].Season);
                if (leftSeason != rightSeason) return leftSeason < rightSeason;
                return *videos[left].Episode < *videos[right].Episode;
            });
        }

        auto const current = std::find_if(order.begin(), order.end(), [&videos, currentVideoId](std::size_t index)
        {
            return videos[index].Id && *videos[index].Id == currentVideoId;
        });
        if (current == order.end() || current + 1 == order.end()) return std::nullopt;
        auto const next = *(current + 1);
        if (videos[next].ReleasedAtMs && *videos[next].ReleasedAtMs > nowMs) return std::nullopt;
        return next;
    }

    std::optional<std::int64_t> ParseIsoDateMs(std::wstring_view text)
    {
        std::size_t position{};
        auto const year = Digits(text, position, 4);
        if (!year) return std::nullopt;
        std::int64_t month = 1;
        std::int64_t day = 1;
        if (position < text.size() && text[position] == L'-')
        {
            ++position;
            auto const parsedMonth = Digits(text, position, 2);
            if (!parsedMonth) return std::nullopt;
            month = *parsedMonth;
            if (position < text.size() && text[position] == L'-')
            {
                ++position;
                auto const parsedDay = Digits(text, position, 2);
                if (!parsedDay) return std::nullopt;
                day = *parsedDay;
            }
        }
        if (month < 1 || month > 12 || day < 1 || day > DaysInMonth(*year, month)) return std::nullopt;

        std::int64_t milliseconds{};
        std::int64_t offsetMinutes{};
        if (position < text.size() && (text[position] == L'T' || text[position] == L' '))
        {
            ++position;
            auto const hour = Digits(text, position, 2);
            if (!hour || position >= text.size() || text[position] != L':') return std::nullopt;
            ++position;
            auto const minute = Digits(text, position, 2);
            if (!minute) return std::nullopt;
            std::int64_t second{};
            std::int64_t fraction{};
            if (position < text.size() && text[position] == L':')
            {
                ++position;
                auto const parsedSecond = Digits(text, position, 2);
                if (!parsedSecond) return std::nullopt;
                second = *parsedSecond;
                if (position < text.size() && text[position] == L'.')
                {
                    ++position;
                    std::size_t digits{};
                    std::int64_t scale = 100;
                    while (position < text.size() && IsAsciiDigit(text[position]))
                    {
                        if (digits < 3)
                        {
                            fraction += (text[position] - L'0') * scale;
                            scale /= 10;
                        }
                        ++digits;
                        ++position;
                    }
                    if (digits == 0) return std::nullopt;
                }
            }
            if (*hour > 24 || *minute > 59 || second > 59 || (*hour == 24 && (*minute != 0 || second != 0 || fraction != 0)))
            {
                return std::nullopt;
            }
            if (position < text.size() && text[position] == L'Z')
            {
                ++position;
            }
            else if (position < text.size() && (text[position] == L'+' || text[position] == L'-'))
            {
                auto const sign = text[position] == L'-' ? -1 : 1;
                ++position;
                auto const offsetHour = Digits(text, position, 2);
                if (!offsetHour || position >= text.size() || text[position] != L':') return std::nullopt;
                ++position;
                auto const offsetMinute = Digits(text, position, 2);
                if (!offsetMinute || *offsetHour > 23 || *offsetMinute > 59) return std::nullopt;
                offsetMinutes = sign * (*offsetHour * 60 + *offsetMinute);
            }
            milliseconds = ((*hour * 60 + *minute) * 60 + second) * 1000 + fraction;
        }
        if (position != text.size()) return std::nullopt;
        return DaysFromCivil(*year, month, day) * 86400000 + milliseconds - offsetMinutes * 60000;
    }

    bool IsImdbId(std::wstring_view id) noexcept
    {
        if (id.size() < 3 || id.size() > 12 || id.substr(0, 2) != L"tt") return false;
        return std::all_of(id.begin() + 2, id.end(), IsAsciiDigit);
    }
}
