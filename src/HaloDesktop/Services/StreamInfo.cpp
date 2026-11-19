#include "pch.h"
#include "Services/StreamInfo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
    using HaloDesktop::Services::ParsedStreamInfo;

    std::wstring Value(std::optional<winrt::hstring> const& value)
    {
        return value ? std::wstring(value->c_str()) : std::wstring{};
    }

    // Addon text is searched by every matcher below, so each field is bounded
    // before it is: a real name, title or description is a few hundred
    // characters, while the mapper lets each through at up to 4096.
    constexpr std::size_t MaximumSearchedFieldLength = 1024;
    // Longer digit runs cannot be a size (the bytes would not fit), and a long
    // run is what makes the size pattern backtrack quadratically.
    constexpr std::size_t MaximumSizeDigitRun = 12;

    std::wstring SearchText(HaloDesktop::Api::Dto::StreamRecord const& stream)
    {
        std::wstring result;
        for (auto const& value : { stream.Name, stream.Title, stream.Description, stream.Filename })
        {
            if (!value || value->empty()) continue;
            if (!result.empty()) result.push_back(L'\n');
            std::wstring_view text{ *value };
            result.append(text.substr(0, MaximumSearchedFieldLength));
        }
        return result;
    }

    std::wregex Pattern(wchar_t const* pattern, bool insensitive = true)
    {
        auto flags = std::regex_constants::ECMAScript;
        if (insensitive) flags |= std::regex_constants::icase;
        return std::wregex(pattern, flags);
    }

    // The patterns are compiled once (function-local statics), and a const
    // regex may be searched from several threads at once. A search the engine
    // gives up on (its complexity or stack limit) reads as no match.
    bool Matches(std::wstring const& text, std::wregex const& pattern)
    {
        try
        {
            return std::regex_search(text, pattern);
        }
        catch (std::regex_error const&)
        {
            return false;
        }
    }

    bool Search(std::wstring const& text, std::wsmatch& match, std::wregex const& pattern)
    {
        try
        {
            return std::regex_search(text, match, pattern);
        }
        catch (std::regex_error const&)
        {
            return false;
        }
    }

    // Replaces every digit run longer than MaximumSizeDigitRun with one '_', a
    // character that is neither a digit nor a space, so the run can neither be
    // read as a size nor let the number before it reach a unit after it.
    // Shorter runs are kept as they are.
    std::wstring WithBoundedDigitRuns(std::wstring const& text)
    {
        std::wstring result;
        result.reserve(text.size());
        std::size_t runStart = 0;
        auto const flush = [&result, &text, &runStart](std::size_t runEnd)
        {
            if (runEnd - runStart > MaximumSizeDigitRun) result.push_back(L'_');
            else result.append(text, runStart, runEnd - runStart);
        };
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            auto const value = text[index];
            if (value >= L'0' && value <= L'9')
            {
                if (index == 0 || text[index - 1] < L'0' || text[index - 1] > L'9') runStart = index;
                continue;
            }
            if (index > 0 && text[index - 1] >= L'0' && text[index - 1] <= L'9') flush(index);
            result.push_back(value);
        }
        if (!text.empty() && text.back() >= L'0' && text.back() <= L'9') flush(text.size());
        return result;
    }

    bool IsLineEnd(wchar_t value) noexcept
    {
        return value == L'\n' || value == L'\r' || value == L'\x2028' || value == L'\x2029';
    }

    // Tokens that read as a quality but are not: the DTS-HD audio codec ("HD"
    // would claim 720p) and a `.ts` file extension ending a line ("TS" would
    // claim a telesync). The reference writes this as one multiline regex,
    // /\bdts[-. ]?hd\b|\.ts$/gim. MSVC's ECMAScript grammar has no portable
    // multiline flag, so the `.ts` half is matched by hand at every line end
    // (the same terminators JavaScript's `m` flag honours) instead of trusting
    // what `$` means there.
    std::wstring WithoutQualityLookalikes(std::wstring const& source)
    {
        static std::wregex const dtsHd(LR"(\bdts[-. ]?hd\b)", std::regex_constants::ECMAScript | std::regex_constants::icase);
        auto text = std::regex_replace(source, dtsHd, L" ");
        std::wstring result;
        result.reserve(text.size());
        for (std::size_t index{}; index < text.size(); ++index)
        {
            auto const endsHere = index + 3 == text.size() || (index + 3 < text.size() && IsLineEnd(text[index + 3]));
            if (endsHere
                && text[index] == L'.'
                && std::towlower(text[index + 1]) == L't'
                && std::towlower(text[index + 2]) == L's')
            {
                result.push_back(L' ');
                index += 2;
                continue;
            }
            result.push_back(text[index]);
        }
        return result;
    }

    std::optional<winrt::hstring> MatchQuality(std::wstring const& source)
    {
        auto const text = WithoutQualityLookalikes(source);
        static auto const uhd = Pattern(LR"(\b(2160p?|4k|uhd)\b)");
        static auto const qhd = Pattern(LR"(\b1440p\b)");
        static auto const fullHd = Pattern(LR"(\b1080p?\b|\bfullhd\b)");
        static auto const hd = Pattern(LR"(\b720p?\b|\bhd\b)");
        static auto const sd480 = Pattern(LR"(\b480p?\b)");
        static auto const sd = Pattern(LR"(\b(sd|cam|ts)\b)");
        if (Matches(text, uhd)) return L"2160p";
        if (Matches(text, qhd)) return L"1440p";
        if (Matches(text, fullHd)) return L"1080p";
        if (Matches(text, hd)) return L"720p";
        if (Matches(text, sd480)) return L"480p";
        if (Matches(text, sd)) return L"SD";
        return std::nullopt;
    }

    std::optional<winrt::hstring> MatchDynamicRange(std::wstring const& text)
    {
        static auto const dolbyVision = Pattern(LR"(\b(dolby[ .]?vision|dovi)\b)");
        static auto const dv = Pattern(LR"(\bDV\b)", false);
        static auto const hdr10 = Pattern(LR"(\bhdr10\+?\b)");
        static auto const hlg = Pattern(LR"(\bhlg\b)");
        static auto const hdr = Pattern(LR"(\bhdr\b)");
        if (Matches(text, dolbyVision) || Matches(text, dv)) return L"DV";
        if (Matches(text, hdr10)) return L"HDR10";
        if (Matches(text, hlg)) return L"HLG";
        if (Matches(text, hdr)) return L"HDR";
        return std::nullopt;
    }

    std::optional<winrt::hstring> MatchCodec(std::wstring const& text)
    {
        static auto const tenBitPattern = Pattern(LR"(\b10[ .-]?bit\b)");
        static auto const hevc = Pattern(LR"(\b(hevc|h[ .]?265|x265)\b)");
        static auto const av1 = Pattern(LR"(\bav1\b)");
        static auto const avc = Pattern(LR"(\b(avc|h[ .]?264|x264)\b)");
        static auto const xvid = Pattern(LR"(\bxvid\b)");
        auto const tenBit = Matches(text, tenBitPattern);
        if (Matches(text, hevc)) return tenBit ? L"HEVC 10-BIT" : L"HEVC";
        if (Matches(text, av1)) return tenBit ? L"AV1 10-BIT" : L"AV1";
        if (Matches(text, avc)) return L"H.264";
        if (Matches(text, xvid)) return L"XVID";
        return std::nullopt;
    }

    winrt::hstring WithChannels(std::wstring const& text, wchar_t const* codec)
    {
        // No leading \b: the channel count is usually glued to the codec ("DDP5.1").
        static auto const channels = Pattern(LR"(([2578])[ .]([01])(?!\d))", false);
        std::wsmatch match;
        if (Search(text, match, channels))
        {
            return winrt::hstring{ std::wstring(codec) + L" " + match[1].str() + L"." + match[2].str() };
        }
        return codec;
    }

    // Each codec ends at anything but a letter rather than at a word break: the
    // channel count is usually glued on (`DDP5.1`, `AAC2.0`), and a digit after
    // the name is no break at all. `DD+` is how many releases spell DDP.
    std::optional<winrt::hstring> MatchAudio(std::wstring const& text)
    {
        static auto const atmos = Pattern(LR"(\batmos(?![A-Za-z]))");
        static auto const trueHd = Pattern(LR"(\btrue[ .]?hd(?![A-Za-z]))");
        static auto const dtsHd = Pattern(LR"(\bdts[ .-]?hd(?![A-Za-z]))");
        static auto const dts = Pattern(LR"(\bdts(?![A-Za-z]))");
        static auto const ddp = Pattern(LR"(\b(ddp|dd\+|eac3|e-ac-3)(?![A-Za-z]))");
        static auto const dd = Pattern(LR"(\b(dd|ac3)(?![A-Za-z]))");
        static auto const aac = Pattern(LR"(\baac(?![A-Za-z]))");
        static auto const opus = Pattern(LR"(\bopus(?![A-Za-z]))");
        static auto const flac = Pattern(LR"(\bflac(?![A-Za-z]))");
        if (Matches(text, atmos)) return L"ATMOS";
        if (Matches(text, trueHd)) return L"TRUEHD";
        if (Matches(text, dtsHd)) return L"DTS-HD";
        if (Matches(text, dts)) return L"DTS";
        if (Matches(text, ddp)) return WithChannels(text, L"DDP");
        if (Matches(text, dd)) return WithChannels(text, L"DD");
        if (Matches(text, aac)) return WithChannels(text, L"AAC");
        if (Matches(text, opus)) return L"OPUS";
        if (Matches(text, flac)) return L"FLAC";
        return std::nullopt;
    }

    std::optional<std::uint64_t> MatchSize(std::wstring const& source)
    {
        static auto const size = Pattern(LR"((\d+(?:[.,]\d+)?)\s*(GB|GiB|MB|MiB)\b)");
        auto const text = WithBoundedDigitRuns(source);
        std::wsmatch match;
        if (!Search(text, match, size)) return std::nullopt;
        auto number = match[1].str();
        std::replace(number.begin(), number.end(), L',', L'.');
        try
        {
            auto const value = std::stod(number);
            if (!std::isfinite(value) || value <= 0) return std::nullopt;
            auto const multiplier = std::towupper(match[2].str()[0]) == L'G' ? 1024.0 * 1024.0 * 1024.0 : 1024.0 * 1024.0;
            return HaloDesktop::Services::WholeBytes(value * multiplier);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<bool> MatchCached(std::wstring const& text)
    {
        static auto const cached = Pattern(LR"(\[\w{2,3}\+\]|\b(cached|instant)\b)");
        static auto const download = Pattern(LR"(\[\w{2,3}\s*download\])");
        if (Matches(text, cached) || text.find(L'\x26A1') != std::wstring::npos) return true;
        if (Matches(text, download)) return false;
        return std::nullopt;
    }

    std::vector<winrt::hstring> MatchLanguages(std::wstring const& text)
    {
        static std::wregex const pattern(LR"(\b(ENG|ENGLISH|JPN|JAPANESE|GER|FRE|FRENCH|SPA|SPANISH|ITA|KOR|CHI|HIN|RUS|POR|DUT|NOR|SWE|DAN|FIN|POL|TUR|ARA|MULTI|DUAL)\b)", std::regex_constants::icase);
        std::vector<winrt::hstring> result;
        std::set<std::wstring> found;
        for (std::wsregex_iterator it(text.begin(), text.end(), pattern), end; it != end && result.size() < 4; ++it)
        {
            auto token = (*it)[0].str();
            std::transform(token.begin(), token.end(), token.begin(), [](wchar_t value) { return std::towupper(value); });
            if (token == L"ENGLISH") token = L"ENG";
            else if (token == L"JAPANESE") token = L"JPN";
            else if (token == L"FRENCH") token = L"FRE";
            else if (token == L"SPANISH") token = L"SPA";
            if (found.insert(token).second) result.emplace_back(token);
        }
        return result;
    }

    std::wstring FirstLine(std::optional<winrt::hstring> const& value)
    {
        auto text = Value(value);
        auto const end = text.find_first_of(L"\r\n");
        if (end != std::wstring::npos) text.resize(end);
        auto const begin = text.find_first_not_of(L" \t");
        if (begin == std::wstring::npos) return {};
        auto const last = text.find_last_not_of(L" \t");
        return text.substr(begin, last - begin + 1);
    }

    std::wstring StripPictographs(std::wstring const& text)
    {
        std::wstring result;
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            auto const value = text[index];
            if (value >= 0xD800 && value <= 0xDBFF && index + 1 < text.size() && text[index + 1] >= 0xDC00 && text[index + 1] <= 0xDFFF)
            {
                ++index;
                continue;
            }
            if (value == 0xFE0F || value == 0x26A1 || (value >= 0x2600 && value <= 0x27BF)) continue;
            result.push_back(value);
        }
        return result;
    }

    winrt::hstring DetailLine(HaloDesktop::Api::Dto::StreamRecord const& stream, std::wstring const& filename)
    {
        auto raw = Value(stream.Title);
        if (raw.empty()) raw = Value(stream.Description);
        std::wistringstream lines(raw);
        std::wstring line;
        std::wstring result;
        while (std::getline(lines, line))
        {
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            auto const begin = line.find_first_not_of(L" \t");
            if (begin == std::wstring::npos) continue;
            auto const end = line.find_last_not_of(L" \t");
            auto clean = line.substr(begin, end - begin + 1);
            if (clean == filename) continue;
            if (!result.empty()) result.append(L" \x00B7 ");
            result.append(clean);
        }
        result = StripPictographs(result);
        static std::wregex const spaces(LR"(\s{2,})");
        result = std::regex_replace(result, spaces, L" ");
        auto const begin = result.find_first_not_of(L" \t");
        if (begin == std::wstring::npos) return {};
        auto const end = result.find_last_not_of(L" \t");
        return winrt::hstring{ result.substr(begin, end - begin + 1) };
    }

    // Shared by the fallback and by HasIdentifyingFilename so the two cannot drift
    // into disagreeing about which names mean "no name".
    constexpr wchar_t const* UnnamedFilename = L"Unnamed source";

}

namespace HaloDesktop::Services
{
    std::optional<std::uint64_t> WholeBytes(double value) noexcept
    {
        // An exclusive 2^64 ceiling rather than a rounded uint64_t maximum, which
        // a double cannot hold exactly.
        constexpr double Uint64ExclusiveUpperBound = 18446744073709551616.0;
        if (!std::isfinite(value) || value <= 0.0) return std::nullopt;
        auto const rounded = std::round(value);
        if (rounded <= 0.0 || rounded >= Uint64ExclusiveUpperBound) return std::nullopt;
        return static_cast<std::uint64_t>(rounded);
    }

    int QualityRank(std::optional<winrt::hstring> const& quality) noexcept
    {
        static std::array<wchar_t const*, 6> const order{ L"2160p", L"1440p", L"1080p", L"720p", L"480p", L"SD" };
        if (!quality) return static_cast<int>(order.size());
        auto const found = std::find_if(order.begin(), order.end(), [&](wchar_t const* item) { return *quality == item; });
        return found == order.end() ? static_cast<int>(order.size()) : static_cast<int>(std::distance(order.begin(), found));
    }

    bool HasIdentifyingFilename(ParsedStreamInfo const& info) noexcept
    {
        return !info.Filename.empty() && info.Filename != UnnamedFilename;
    }

    ParsedStreamInfo ParseStreamInfo(Api::Dto::StreamRecord const& stream)
    {
        auto const text = SearchText(stream);
        auto filename = Value(stream.Filename);
        if (filename.empty()) filename = FirstLine(stream.Title);
        if (filename.empty()) filename = FirstLine(stream.Description);
        if (filename.empty()) filename = FirstLine(stream.Name);
        if (filename.empty()) filename = UnnamedFilename;

        ParsedStreamInfo result;
        result.Filename = filename;
        result.Quality = MatchQuality(text);
        result.DynamicRange = MatchDynamicRange(text);
        result.Codec = MatchCodec(text);
        result.Audio = MatchAudio(text);
        // A hint of zero says nothing, so the text gets its turn.
        result.SizeBytes = stream.VideoSize && *stream.VideoSize > 0 ? stream.VideoSize : MatchSize(text);
        result.Cached = MatchCached(text);
        result.Languages = MatchLanguages(text);
        result.Detail = DetailLine(stream, filename);
        return result;
    }

    int CompareStreams(ParsedStreamInfo const& left, ParsedStreamInfo const& right) noexcept
    {
        auto const leftCached = left.Cached == true ? 0 : 1;
        auto const rightCached = right.Cached == true ? 0 : 1;
        if (leftCached != rightCached) return leftCached - rightCached;
        auto const quality = QualityRank(left.Quality) - QualityRank(right.Quality);
        if (quality != 0) return quality;
        auto const leftSize = left.SizeBytes.value_or(0);
        auto const rightSize = right.SizeBytes.value_or(0);
        if (leftSize == rightSize) return 0;
        return leftSize > rightSize ? -1 : 1;
    }

    winrt::hstring FormatStreamSize(std::optional<std::uint64_t> bytes)
    {
        if (!bytes) return L"UNKNOWN";
        auto const gib = static_cast<long double>(*bytes) / (1024.0L * 1024.0L * 1024.0L);
        std::wostringstream output;
        output << std::fixed << std::setprecision(gib >= 10 ? 1 : 2) << gib << L" GB";
        return winrt::hstring{ output.str() };
    }
}
