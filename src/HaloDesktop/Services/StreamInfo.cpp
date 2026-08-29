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

namespace
{
    using HaloDesktop::Services::ParsedStreamInfo;

    std::wstring Value(std::optional<winrt::hstring> const& value)
    {
        return value ? std::wstring(value->c_str()) : std::wstring{};
    }

    std::wstring SearchText(HaloDesktop::Api::Dto::StreamRecord const& stream)
    {
        std::wstring result;
        for (auto const& value : { stream.Name, stream.Title, stream.Description, stream.Filename })
        {
            if (!value || value->empty()) continue;
            if (!result.empty()) result.push_back(L'\n');
            result.append(value->c_str());
        }
        return result;
    }

    bool Matches(std::wstring const& text, wchar_t const* pattern, bool insensitive = true)
    {
        auto flags = std::regex_constants::ECMAScript;
        if (insensitive) flags |= std::regex_constants::icase;
        return std::regex_search(text, std::wregex(pattern, flags));
    }

    std::optional<winrt::hstring> MatchQuality(std::wstring const& text)
    {
        if (Matches(text, LR"(\b(2160p?|4k|uhd)\b)")) return L"2160p";
        if (Matches(text, LR"(\b1440p\b)")) return L"1440p";
        if (Matches(text, LR"(\b1080p?\b|\bfullhd\b)")) return L"1080p";
        if (Matches(text, LR"(\b720p?\b|\bhd\b)")) return L"720p";
        if (Matches(text, LR"(\b480p?\b)")) return L"480p";
        if (Matches(text, LR"(\b(sd|cam|ts)\b)")) return L"SD";
        return std::nullopt;
    }

    std::optional<winrt::hstring> MatchDynamicRange(std::wstring const& text)
    {
        if (Matches(text, LR"(\b(dolby[ .]?vision|dovi)\b)") || Matches(text, LR"(\bDV\b)", false)) return L"DV";
        if (Matches(text, LR"(\bhdr10\+?\b)")) return L"HDR10";
        if (Matches(text, LR"(\bhlg\b)")) return L"HLG";
        if (Matches(text, LR"(\bhdr\b)")) return L"HDR";
        return std::nullopt;
    }

    std::optional<winrt::hstring> MatchCodec(std::wstring const& text)
    {
        auto const tenBit = Matches(text, LR"(\b10[ .-]?bit\b)");
        if (Matches(text, LR"(\b(hevc|h[ .]?265|x265)\b)")) return tenBit ? L"HEVC 10-BIT" : L"HEVC";
        if (Matches(text, LR"(\bav1\b)")) return tenBit ? L"AV1 10-BIT" : L"AV1";
        if (Matches(text, LR"(\b(avc|h[ .]?264|x264)\b)")) return L"H.264";
        if (Matches(text, LR"(\bxvid\b)")) return L"XVID";
        return std::nullopt;
    }

    winrt::hstring WithChannels(std::wstring const& text, wchar_t const* codec)
    {
        std::wsmatch match;
        if (std::regex_search(text, match, std::wregex(LR"(([2578])[ .]([01])(?!\d))")))
        {
            return winrt::hstring{ std::wstring(codec) + L" " + match[1].str() + L"." + match[2].str() };
        }
        return codec;
    }

    std::optional<winrt::hstring> MatchAudio(std::wstring const& text)
    {
        if (Matches(text, LR"(\batmos\b)")) return L"ATMOS";
        if (Matches(text, LR"(\btrue[ .]?hd\b)")) return L"TRUEHD";
        if (Matches(text, LR"(\bdts[ .-]?hd\b)")) return L"DTS-HD";
        if (Matches(text, LR"(\bdts\b)")) return L"DTS";
        if (Matches(text, LR"(\b(ddp|eac3|e-ac-3)\b)")) return WithChannels(text, L"DDP");
        if (Matches(text, LR"(\b(dd|ac3)\b)")) return WithChannels(text, L"DD");
        if (Matches(text, LR"(\baac\b)")) return WithChannels(text, L"AAC");
        if (Matches(text, LR"(\bopus\b)")) return L"OPUS";
        if (Matches(text, LR"(\bflac\b)")) return L"FLAC";
        return std::nullopt;
    }

    std::optional<std::uint64_t> MatchSize(std::wstring const& text)
    {
        std::wsmatch match;
        if (!std::regex_search(text, match, std::wregex(LR"((\d+(?:[.,]\d+)?)\s*(GB|GiB|MB|MiB)\b)", std::regex_constants::icase))) return std::nullopt;
        auto number = match[1].str();
        std::replace(number.begin(), number.end(), L',', L'.');
        try
        {
            auto const value = std::stold(number);
            if (!std::isfinite(value) || value <= 0) return std::nullopt;
            auto const multiplier = std::towupper(match[2].str()[0]) == L'G' ? 1024.0L * 1024.0L * 1024.0L : 1024.0L * 1024.0L;
            auto const bytes = value * multiplier;
            // long double is double precision on MSVC, so compare against an
            // exclusive 2^64 ceiling instead of a rounded uint64_t maximum.
            constexpr long double Uint64ExclusiveUpperBound = 18446744073709551616.0L;
            if (!std::isfinite(bytes) || bytes <= 0 || bytes >= Uint64ExclusiveUpperBound)
            {
                return std::nullopt;
            }
            return static_cast<std::uint64_t>(bytes);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<bool> MatchCached(std::wstring const& text)
    {
        if (Matches(text, LR"(\[\w{2,3}\+\]|\b(cached|instant)\b)") || text.find(L'\x26A1') != std::wstring::npos) return true;
        if (Matches(text, LR"(\[\w{2,3}\s*download\])")) return false;
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
        result = std::regex_replace(result, std::wregex(LR"(\s{2,})"), L" ");
        auto const begin = result.find_first_not_of(L" \t");
        if (begin == std::wstring::npos) return {};
        auto const end = result.find_last_not_of(L" \t");
        return winrt::hstring{ result.substr(begin, end - begin + 1) };
    }

    int QualityRank(std::optional<winrt::hstring> const& quality) noexcept
    {
        static std::array<wchar_t const*, 6> const order{ L"2160p", L"1440p", L"1080p", L"720p", L"480p", L"SD" };
        if (!quality) return static_cast<int>(order.size());
        auto const found = std::find_if(order.begin(), order.end(), [&](wchar_t const* item) { return *quality == item; });
        return found == order.end() ? static_cast<int>(order.size()) : static_cast<int>(std::distance(order.begin(), found));
    }
}

namespace HaloDesktop::Services
{
    winrt::hstring BuildSourceTagLine(ParsedStreamInfo const& info)
    {
        std::wstring result;
        for (auto const& value : { info.Quality, info.DynamicRange, info.Codec, info.Audio })
        {
            if (!value || value->empty()) continue;
            if (!result.empty()) result.append(L" \x00B7 ");
            result.append(value->c_str());
        }
        return result.empty() ? winrt::hstring{ L"Source" } : winrt::hstring{ result };
    }

    ParsedStreamInfo ParseStreamInfo(Api::Dto::StreamRecord const& stream)
    {
        auto const text = SearchText(stream);
        auto filename = Value(stream.Filename);
        if (filename.empty()) filename = FirstLine(stream.Title);
        if (filename.empty()) filename = FirstLine(stream.Description);
        if (filename.empty()) filename = FirstLine(stream.Name);
        if (filename.empty()) filename = L"Unnamed source";

        ParsedStreamInfo result;
        result.Filename = filename;
        result.Quality = MatchQuality(text);
        result.DynamicRange = MatchDynamicRange(text);
        result.Codec = MatchCodec(text);
        result.Audio = MatchAudio(text);
        result.SizeBytes = stream.VideoSize ? stream.VideoSize : MatchSize(text);
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
