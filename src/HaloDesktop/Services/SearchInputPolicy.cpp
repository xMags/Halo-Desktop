#include "Services/SearchInputPolicy.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using HaloDesktop::Services::SearchInput;
    using HaloDesktop::Services::SearchInputKind;

    constexpr wchar_t LocalFileReason[] = L"Files on this PC can't be opened from Search.";
    constexpr wchar_t OtherSchemeReason[] = L"This kind of link isn't supported.";
    constexpr wchar_t TooLongReason[] = L"This link is too long.";
    constexpr wchar_t CredentialsReason[] = L"Links with a user name or password aren't supported.";
    constexpr wchar_t InvalidLinkReason[] = L"This link isn't valid.";
    constexpr wchar_t MagnetV2Reason[] = L"This magnet link uses the newer v2 format, which isn't supported.";
    constexpr wchar_t MagnetInvalidReason[] = L"This magnet link has no valid info hash.";
    constexpr wchar_t ImdbOtherReason[] = L"This IMDb link isn't a title or a person.";
    constexpr wchar_t TvdbOtherReason[] = L"This TVDB link isn't a film or a series.";

    // Schemes that are links although no `//` follows them. Anything else
    // with a colon in it (`Re:Zero`, `Dune: Part Two`) stays words to search.
    constexpr std::array<std::wstring_view, 12> OpaqueSchemes{
        L"magnet", L"file", L"data", L"javascript", L"vbscript", L"mailto",
        L"about", L"blob", L"urn", L"ed2k", L"acestream", L"stremio",
    };

    constexpr std::array<std::wstring_view, 3> VideoSiteHosts{ L"youtube.com", L"youtu.be", L"twitch.tv" };

    constexpr wchar_t AsciiLower(wchar_t value) noexcept
    {
        return value >= L'A' && value <= L'Z' ? static_cast<wchar_t>(value - L'A' + L'a') : value;
    }

    std::wstring LowerAscii(std::wstring_view text)
    {
        std::wstring lower;
        lower.reserve(text.size());
        for (auto const value : text) lower.push_back(AsciiLower(value));
        return lower;
    }

    bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right) noexcept
    {
        if (left.size() != right.size()) return false;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            if (AsciiLower(left[index]) != AsciiLower(right[index])) return false;
        }
        return true;
    }

    bool StartsWithIgnoreCase(std::wstring_view text, std::wstring_view prefix) noexcept
    {
        return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
    }

    constexpr bool IsDigit(wchar_t value) noexcept { return value >= L'0' && value <= L'9'; }
    constexpr bool IsAsciiLetter(wchar_t value) noexcept
    {
        return (value >= L'a' && value <= L'z') || (value >= L'A' && value <= L'Z');
    }

    bool AllDigits(std::wstring_view text) noexcept
    {
        if (text.empty()) return false;
        for (auto const value : text)
        {
            if (!IsDigit(value)) return false;
        }
        return true;
    }

    int HexValue(wchar_t value) noexcept
    {
        if (IsDigit(value)) return value - L'0';
        auto const lower = AsciiLower(value);
        if (lower >= L'a' && lower <= L'f') return lower - L'a' + 10;
        return -1;
    }

    // `prefix` (two letters, any case) and 7 to 10 digits, as IMDb's ids are;
    // lower case. 6 or 11 digits is not an id.
    std::optional<std::wstring> ImdbStyleId(std::wstring_view text, std::wstring_view prefix)
    {
        if (!StartsWithIgnoreCase(text, prefix)) return std::nullopt;
        auto const digits = text.substr(prefix.size());
        if (digits.size() < 7 || digits.size() > 10 || !AllDigits(digits)) return std::nullopt;
        return std::wstring{ prefix } + std::wstring{ digits };
    }

    void AppendUtf8(std::string& bytes, char32_t point)
    {
        if (point < 0x80)
        {
            bytes.push_back(static_cast<char>(point));
        }
        else if (point < 0x800)
        {
            bytes.push_back(static_cast<char>(0xC0 | (point >> 6)));
            bytes.push_back(static_cast<char>(0x80 | (point & 0x3F)));
        }
        else if (point < 0x10000)
        {
            bytes.push_back(static_cast<char>(0xE0 | (point >> 12)));
            bytes.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3F)));
            bytes.push_back(static_cast<char>(0x80 | (point & 0x3F)));
        }
        else
        {
            bytes.push_back(static_cast<char>(0xF0 | (point >> 18)));
            bytes.push_back(static_cast<char>(0x80 | ((point >> 12) & 0x3F)));
            bytes.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3F)));
            bytes.push_back(static_cast<char>(0x80 | (point & 0x3F)));
        }
    }

    void AppendUtf16(std::wstring& text, char32_t point)
    {
        if (point < 0x10000)
        {
            text.push_back(static_cast<wchar_t>(point));
            return;
        }
        point -= 0x10000;
        text.push_back(static_cast<wchar_t>(0xD800 + (point >> 10)));
        text.push_back(static_cast<wchar_t>(0xDC00 + (point & 0x3FF)));
    }

    // Strict UTF-8; anything malformed (overlong, surrogate, truncated) reads
    // as U+FFFD, so a hostile `%`-sequence cannot smuggle anything through.
    std::wstring DecodeUtf8(std::string_view bytes)
    {
        constexpr char32_t Replacement = 0xFFFD;
        std::wstring text;
        text.reserve(bytes.size());
        std::size_t index = 0;
        while (index < bytes.size())
        {
            auto const lead = static_cast<unsigned char>(bytes[index]);
            if (lead < 0x80)
            {
                text.push_back(static_cast<wchar_t>(lead));
                ++index;
                continue;
            }
            std::size_t length{};
            char32_t point{};
            char32_t minimum{};
            if ((lead & 0xE0) == 0xC0) { length = 2; point = lead & 0x1F; minimum = 0x80; }
            else if ((lead & 0xF0) == 0xE0) { length = 3; point = lead & 0x0F; minimum = 0x800; }
            else if ((lead & 0xF8) == 0xF0) { length = 4; point = lead & 0x07; minimum = 0x10000; }
            else
            {
                AppendUtf16(text, Replacement);
                ++index;
                continue;
            }
            auto valid = index + length <= bytes.size();
            for (std::size_t offset = 1; valid && offset < length; ++offset)
            {
                auto const next = static_cast<unsigned char>(bytes[index + offset]);
                if ((next & 0xC0) != 0x80)
                {
                    valid = false;
                    break;
                }
                point = (point << 6) | (next & 0x3F);
            }
            if (!valid || point < minimum || point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF))
            {
                AppendUtf16(text, Replacement);
                ++index;
                continue;
            }
            AppendUtf16(text, point);
            index += length;
        }
        return text;
    }

    // `%XX` as bytes of UTF-8 (and `+` as a space in a query value); the
    // characters already in the text are kept as they are.
    std::wstring PercentDecoded(std::wstring_view text, bool plusIsSpace)
    {
        std::string bytes;
        bytes.reserve(text.size());
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            auto const value = text[index];
            if (value == L'%' && index + 2 < text.size())
            {
                auto const high = HexValue(text[index + 1]);
                auto const low = HexValue(text[index + 2]);
                if (high >= 0 && low >= 0)
                {
                    bytes.push_back(static_cast<char>((high << 4) | low));
                    index += 2;
                    continue;
                }
            }
            if (value == L'+' && plusIsSpace)
            {
                bytes.push_back(' ');
                continue;
            }
            char32_t point = value;
            if (value >= 0xD800 && value <= 0xDBFF && index + 1 < text.size()
                && text[index + 1] >= 0xDC00 && text[index + 1] <= 0xDFFF)
            {
                point = 0x10000 + ((static_cast<char32_t>(value) - 0xD800) << 10) + (static_cast<char32_t>(text[index + 1]) - 0xDC00);
                ++index;
            }
            else if (value >= 0xD800 && value <= 0xDFFF)
            {
                point = 0xFFFD;
            }
            AppendUtf8(bytes, point);
        }
        return DecodeUtf8(bytes);
    }

    // Control characters, and the direction overrides that could make a name
    // read as something it is not.
    constexpr bool IsHiddenCharacter(wchar_t value) noexcept
    {
        return value < 0x20 || (value >= 0x7F && value <= 0x9F)
            || (value >= 0x202A && value <= 0x202E) || (value >= 0x2066 && value <= 0x2069)
            || value == 0x200E || value == 0x200F;
    }

    constexpr bool IsSpace(wchar_t value) noexcept
    {
        return value == L' ' || value == 0x00A0 || value == 0x3000 || (value >= 0x2000 && value <= 0x200B);
    }

    // What may be shown of a name taken from a link: no hidden characters,
    // no surrounding space, and at most `limit` UTF-16 units without cutting
    // a surrogate pair in half.
    std::wstring Displayable(std::wstring_view text, std::size_t limit)
    {
        std::wstring shown;
        shown.reserve((std::min)(text.size(), limit));
        for (auto const value : text)
        {
            if (!IsHiddenCharacter(value)) shown.push_back(value);
        }
        std::size_t first = 0;
        while (first < shown.size() && IsSpace(shown[first])) ++first;
        auto last = shown.size();
        while (last > first && IsSpace(shown[last - 1])) --last;
        shown = shown.substr(first, last - first);
        if (shown.size() > limit)
        {
            auto length = limit;
            if (length > 0 && shown[length - 1] >= 0xD800 && shown[length - 1] <= 0xDBFF) --length;
            shown.resize(length);
            while (!shown.empty() && IsSpace(shown.back())) shown.pop_back();
        }
        return shown;
    }

    SearchInput Unsupported(std::wstring reason)
    {
        SearchInput input;
        input.Kind = SearchInputKind::UnsupportedLink;
        input.Reason = std::move(reason);
        return input;
    }

    bool IsDrivePath(std::wstring_view text) noexcept
    {
        return text.size() >= 3 && IsAsciiLetter(text[0]) && text[1] == L':' && (text[2] == L'\\' || text[2] == L'/');
    }

    bool IsUncPath(std::wstring_view text) noexcept
    {
        return text.starts_with(L"\\\\") || text.starts_with(L"//");
    }

    // The scheme, lower case, when `text` is a link: a valid scheme of two or
    // more characters followed by `//`, or one of the opaque schemes.
    std::optional<std::wstring> LinkScheme(std::wstring_view text)
    {
        auto const colon = text.find(L':');
        if (colon == std::wstring_view::npos || colon < 2 || !IsAsciiLetter(text[0])) return std::nullopt;
        for (std::size_t index = 1; index < colon; ++index)
        {
            auto const value = text[index];
            if (!IsAsciiLetter(value) && !IsDigit(value) && value != L'+' && value != L'-' && value != L'.') return std::nullopt;
        }
        auto scheme = LowerAscii(text.substr(0, colon));
        if (text.substr(colon + 1).starts_with(L"//")) return scheme;
        for (auto const opaque : OpaqueSchemes)
        {
            if (scheme == opaque) return scheme;
        }
        return std::nullopt;
    }

    std::vector<std::wstring_view> PathSegments(std::wstring_view path)
    {
        std::vector<std::wstring_view> segments;
        std::size_t start = 0;
        while (start <= path.size())
        {
            auto end = path.find(L'/', start);
            if (end == std::wstring_view::npos) end = path.size();
            if (end > start) segments.push_back(path.substr(start, end - start));
            start = end + 1;
        }
        return segments;
    }

    // `key=value` pairs of a query, values left encoded.
    std::vector<std::pair<std::wstring, std::wstring_view>> QueryPairs(std::wstring_view query)
    {
        std::vector<std::pair<std::wstring, std::wstring_view>> pairs;
        std::size_t start = 0;
        while (start <= query.size())
        {
            auto end = query.find(L'&', start);
            if (end == std::wstring_view::npos) end = query.size();
            auto const part = query.substr(start, end - start);
            if (auto const equals = part.find(L'='); equals != std::wstring_view::npos)
            {
                pairs.emplace_back(LowerAscii(part.substr(0, equals)), part.substr(equals + 1));
            }
            start = end + 1;
        }
        return pairs;
    }

    bool HostIsOrUnder(std::wstring_view host, std::wstring_view domain)
    {
        if (host == domain) return true;
        return host.size() > domain.size() && host.ends_with(domain) && host[host.size() - domain.size() - 1] == L'.';
    }

    // RFC 4648 base32 (A to Z, 2 to 7), any case, 32 characters: 20 bytes.
    std::optional<std::wstring> Base32ToHex(std::wstring_view text)
    {
        if (text.size() != 32) return std::nullopt;
        std::wstring hex;
        hex.reserve(40);
        std::uint32_t buffer{};
        int bits{};
        constexpr wchar_t Digits[] = L"0123456789abcdef";
        for (auto const value : text)
        {
            auto const lower = AsciiLower(value);
            std::uint32_t digit{};
            if (lower >= L'a' && lower <= L'z') digit = static_cast<std::uint32_t>(lower - L'a');
            else if (lower >= L'2' && lower <= L'7') digit = static_cast<std::uint32_t>(lower - L'2' + 26);
            else return std::nullopt;
            buffer = (buffer << 5) | digit;
            bits += 5;
            while (bits >= 4)
            {
                bits -= 4;
                hex.push_back(Digits[(buffer >> bits) & 0xF]);
            }
        }
        return hex;
    }

    // 40 hex characters or 32 base32 ones, as lower-case hex.
    std::optional<std::wstring> NormalizedInfoHash(std::wstring_view text)
    {
        if (text.size() == 40)
        {
            for (auto const value : text)
            {
                if (HexValue(value) < 0) return std::nullopt;
            }
            return LowerAscii(text);
        }
        return Base32ToHex(text);
    }

    bool IsMagnetTopicKey(std::wstring_view key) noexcept
    {
        return key == L"xt" || (key.starts_with(L"xt.") && AllDigits(key.substr(3)));
    }

    SearchInput ClassifyMagnet(std::wstring_view text)
    {
        constexpr std::wstring_view Prefix{ L"magnet:?" };
        if (!StartsWithIgnoreCase(text, Prefix)) return Unsupported(MagnetInvalidReason);
        auto query = text.substr(Prefix.size());
        if (auto const fragment = query.find(L'#'); fragment != std::wstring_view::npos) query = query.substr(0, fragment);

        std::set<std::wstring> hashes;
        bool invalidHash{};
        bool sawV2{};
        std::optional<std::wstring> name;
        for (auto const& [key, value] : QueryPairs(query))
        {
            if (IsMagnetTopicKey(key))
            {
                auto const topic = PercentDecoded(value, false);
                if (StartsWithIgnoreCase(topic, L"urn:btih:"))
                {
                    if (auto hash = NormalizedInfoHash(std::wstring_view{ topic }.substr(9))) hashes.insert(std::move(*hash));
                    else invalidHash = true;
                }
                else if (StartsWithIgnoreCase(topic, L"urn:btmh:"))
                {
                    sawV2 = true;
                }
            }
            else if (key == L"dn" && !name)
            {
                name = Displayable(PercentDecoded(value, true), HaloDesktop::Services::SearchMagnetNameMaxLength);
            }
            // `tr` and every other key are dropped: no tracker is ever kept.
        }
        if (!invalidHash && hashes.size() == 1)
        {
            SearchInput input;
            input.Kind = SearchInputKind::Magnet;
            input.InfoHashHex = *hashes.begin();
            input.DisplayName = name.value_or(L"");
            return input;
        }
        if (!invalidHash && hashes.empty() && sawV2) return Unsupported(MagnetV2Reason);
        return Unsupported(MagnetInvalidReason);
    }

    SearchInput ClassifyImdb(std::vector<std::wstring_view> segments)
    {
        // IMDb's localised pages put the language first: `/fr/title/tt...`.
        if (segments.size() > 2 && (segments[0].size() == 2 || (segments[0].size() == 5 && segments[0][2] == L'-')))
        {
            segments.erase(segments.begin());
        }
        if (segments.size() >= 2 && EqualsIgnoreCase(segments[0], L"title"))
        {
            if (auto id = ImdbStyleId(segments[1], L"tt"))
            {
                SearchInput input;
                input.Kind = SearchInputKind::ImdbTitle;
                input.ImdbId = std::move(*id);
                return input;
            }
        }
        if (segments.size() >= 2 && EqualsIgnoreCase(segments[0], L"name"))
        {
            if (auto id = ImdbStyleId(segments[1], L"nm"))
            {
                SearchInput input;
                input.Kind = SearchInputKind::ImdbPerson;
                input.PersonId = std::move(*id);
                return input;
            }
        }
        return Unsupported(ImdbOtherReason);
    }

    SearchInput ClassifyTvdb(std::vector<std::wstring_view> const& segments, std::wstring_view query)
    {
        if (segments.size() >= 3 && EqualsIgnoreCase(segments[0], L"dereferrer")
            && (EqualsIgnoreCase(segments[1], L"series") || EqualsIgnoreCase(segments[1], L"movie"))
            && AllDigits(segments[2]) && segments[2].size() <= 10)
        {
            SearchInput input;
            input.Kind = SearchInputKind::TvdbNumeric;
            input.TvdbId = std::wstring{ segments[2] };
            input.TvdbKind = LowerAscii(segments[1]);
            return input;
        }
        if (segments.size() >= 2 && (EqualsIgnoreCase(segments[0], L"series") || EqualsIgnoreCase(segments[0], L"movies")))
        {
            SearchInput input;
            input.Kind = SearchInputKind::TvdbSlug;
            input.TvdbKind = EqualsIgnoreCase(segments[0], L"series") ? L"series" : L"movie";
            input.DisplayName = Displayable(PercentDecoded(segments[1], false), HaloDesktop::Services::SearchMagnetNameMaxLength);
            return input;
        }
        if (segments.empty() || (segments.size() == 1 && EqualsIgnoreCase(segments[0], L"index.php")))
        {
            bool seriesTab{};
            std::optional<std::wstring_view> id;
            for (auto const& [key, value] : QueryPairs(query))
            {
                if (key == L"tab" && EqualsIgnoreCase(value, L"series")) seriesTab = true;
                if (key == L"id" && AllDigits(value) && value.size() <= 10) id = value;
            }
            if (seriesTab && id)
            {
                SearchInput input;
                input.Kind = SearchInputKind::TvdbNumeric;
                input.TvdbId = std::wstring{ *id };
                input.TvdbKind = L"series";
                return input;
            }
        }
        return Unsupported(TvdbOtherReason);
    }

    // What the player calls a pasted link: its file name, else its host.
    std::wstring WebLinkTitle(std::vector<std::wstring_view> const& segments, std::wstring const& host)
    {
        if (!segments.empty())
        {
            auto name = Displayable(PercentDecoded(segments.back(), false), HaloDesktop::Services::SearchWebLinkTitleMaxLength);
            if (!name.empty()) return name;
        }
        return host;
    }

    std::optional<std::wstring> HostOf(std::wstring_view authority)
    {
        std::wstring_view host = authority;
        std::wstring_view port;
        if (authority.starts_with(L'['))
        {
            auto const close = authority.find(L']');
            if (close == std::wstring_view::npos) return std::nullopt;
            host = authority.substr(0, close + 1);
            auto const rest = authority.substr(close + 1);
            if (!rest.empty())
            {
                if (rest[0] != L':') return std::nullopt;
                port = rest.substr(1);
            }
            for (auto const value : host.substr(1, host.size() - 2))
            {
                if (HexValue(value) < 0 && value != L':' && value != L'.') return std::nullopt;
            }
            if (host.size() <= 2) return std::nullopt;
        }
        else
        {
            if (auto const colon = authority.rfind(L':'); colon != std::wstring_view::npos)
            {
                host = authority.substr(0, colon);
                port = authority.substr(colon + 1);
            }
            for (auto const value : host)
            {
                if (!IsAsciiLetter(value) && !IsDigit(value) && value != L'-' && value != L'.' && value != L'_' && value < 0x80)
                {
                    return std::nullopt;
                }
            }
        }
        if (!port.empty())
        {
            if (port.size() > 5 || !AllDigits(port)) return std::nullopt;
            std::uint32_t number{};
            for (auto const value : port) number = number * 10 + static_cast<std::uint32_t>(value - L'0');
            if (number == 0 || number > 65535) return std::nullopt;
        }
        auto lower = LowerAscii(host);
        if (!lower.empty() && lower.back() == L'.') lower.pop_back();
        if (lower.empty() || lower.front() == L'.') return std::nullopt;
        return lower;
    }

    SearchInput ClassifyWeb(std::wstring_view text, std::size_t schemeLength)
    {
        if (text.size() > HaloDesktop::Services::SearchWebLinkMaxLength) return Unsupported(TooLongReason);
        for (auto const value : text)
        {
            // White space, control characters and backslashes have no place in
            // a link, and are read differently by different parsers.
            if (value <= 0x20 || IsHiddenCharacter(value) || IsSpace(value) || value == L'\\') return Unsupported(InvalidLinkReason);
        }
        auto rest = text.substr(schemeLength + 1);
        if (!rest.starts_with(L"//")) return Unsupported(InvalidLinkReason);
        rest.remove_prefix(2);
        auto const authorityEnd = (std::min)(rest.find_first_of(L"/?#"), rest.size());
        auto const authority = rest.substr(0, authorityEnd);
        if (authority.find(L'@') != std::wstring_view::npos) return Unsupported(CredentialsReason);
        auto const host = HostOf(authority);
        if (!host) return Unsupported(InvalidLinkReason);

        auto tail = rest.substr(authorityEnd);
        if (auto const fragment = tail.find(L'#'); fragment != std::wstring_view::npos) tail = tail.substr(0, fragment);
        std::wstring_view query;
        if (auto const question = tail.find(L'?'); question != std::wstring_view::npos)
        {
            query = tail.substr(question + 1);
            tail = tail.substr(0, question);
        }
        auto const segments = PathSegments(tail);

        for (auto const site : VideoSiteHosts)
        {
            if (HostIsOrUnder(*host, site)) return Unsupported(HaloDesktop::Services::SearchUnsupportedVideoSiteReason);
        }
        if (*host == L"imdb.com" || *host == L"www.imdb.com" || *host == L"m.imdb.com") return ClassifyImdb(segments);
        if (HostIsOrUnder(*host, L"imdb.com")) return Unsupported(ImdbOtherReason);
        if (*host == L"thetvdb.com" || *host == L"www.thetvdb.com") return ClassifyTvdb(segments, query);

        SearchInput input;
        input.Kind = SearchInputKind::WebLink;
        input.Url = std::wstring{ text };
        input.Host = *host;
        input.DisplayName = WebLinkTitle(segments, *host);
        return input;
    }
}

namespace HaloDesktop::Services
{
    SearchInput ClassifySearchInput(std::wstring_view trimmed)
    {
        if (trimmed.empty()) return {};
        if (auto id = ImdbStyleId(trimmed, L"tt"))
        {
            SearchInput input;
            input.Kind = SearchInputKind::ImdbTitle;
            input.ImdbId = std::move(*id);
            return input;
        }
        if (IsDrivePath(trimmed) || IsUncPath(trimmed)) return Unsupported(LocalFileReason);
        auto const scheme = LinkScheme(trimmed);
        if (!scheme) return {};
        if (*scheme == L"magnet") return ClassifyMagnet(trimmed);
        if (*scheme == L"http" || *scheme == L"https") return ClassifyWeb(trimmed, scheme->size());
        if (*scheme == L"file") return Unsupported(LocalFileReason);
        return Unsupported(OtherSchemeReason);
    }
}
