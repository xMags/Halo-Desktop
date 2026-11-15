#include "pch.h"
#include "ViewModels/SourcePresentation.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>
#include <vector>

namespace
{
    using HaloDesktop::Sources::NotParsed;
    using HaloDesktop::Sources::QualityTier;
    using HaloDesktop::Sources::SourceStatus;

    constexpr wchar_t const* Separator = L" \x00B7 ";

    std::wstring Upper(std::wstring_view value)
    {
        std::wstring result{ value };
        std::transform(result.begin(), result.end(), result.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towupper(character));
        });
        return result;
    }

    std::wstring_view View(winrt::hstring const& value) noexcept
    {
        return std::wstring_view{ value.c_str(), value.size() };
    }

    // Splits a " . " joined token list, trimming each piece.
    std::vector<winrt::hstring> SplitTokens(winrt::hstring const& value)
    {
        std::vector<winrt::hstring> result;
        std::wstring const text{ value.c_str() };
        std::size_t start{};
        while (start <= text.size())
        {
            auto const found = text.find(L'\x00B7', start);
            auto piece = text.substr(start, found == std::wstring::npos ? std::wstring::npos : found - start);
            auto const first = piece.find_first_not_of(L" \t");
            if (first != std::wstring::npos)
            {
                auto const last = piece.find_last_not_of(L" \t");
                result.emplace_back(piece.substr(first, last - first + 1));
            }
            if (found == std::wstring::npos) break;
            start = found + 1;
        }
        return result;
    }

    std::wstring JoinList(std::vector<winrt::hstring> const& values, wchar_t const* separator)
    {
        std::wstring result;
        for (auto const& value : values)
        {
            if (value.empty()) continue;
            if (!result.empty()) result.append(separator);
            result.append(value.c_str());
        }
        return result;
    }

    // Rounded to the nearest whole number, as the reference's Math.round does for
    // the positive figures this is given.
    winrt::hstring Whole(double value)
    {
        return winrt::to_hstring(static_cast<std::int64_t>(std::llround(value)));
    }

    winrt::hstring Dimensions(QualityTier tier, std::wstring_view quality)
    {
        if (quality == L"1440p") return L"2560 \x00D7 1440";
        if (quality == L"480p") return L"854 \x00D7 480";
        switch (tier)
        {
        case QualityTier::UltraHd: return L"3840 \x00D7 2160";
        case QualityTier::FullHd: return L"1920 \x00D7 1080";
        case QualityTier::Hd: return L"1280 \x00D7 720";
        case QualityTier::Lower: break;
        }
        return {};
    }

    winrt::hstring CodecName(winrt::hstring const& codec)
    {
        if (codec.empty() || View(codec) == NotParsed) return {};
        if (codec == L"HEVC") return L"H.265";
        if (codec == L"HEVC 10-BIT") return L"H.265 10-bit";
        if (codec == L"AV1 10-BIT") return L"AV1 10-bit";
        if (codec == L"XVID") return L"Xvid";
        return codec;
    }

    winrt::hstring RangeDetail(winrt::hstring const& range)
    {
        if (range == L"DV") return L"Dolby Vision";
        if (range == L"HLG") return L"HLG";
        if (range == L"HDR10") return L"HDR10";
        return L"HDR";
    }

    winrt::hstring SoundDetail(winrt::hstring const& audio)
    {
        if (audio.empty() || View(audio) == NotParsed) return {};
        auto const text = Upper(View(audio));
        if (text == L"ATMOS") return L"Dolby Atmos";
        if (text == L"TRUEHD") return L"Dolby TrueHD";
        if (text == L"DTS-HD") return L"DTS-HD";
        if (text == L"DTS") return L"DTS";
        auto const space = text.find(L' ');
        if (space != std::wstring::npos)
        {
            auto const channels = text.substr(space + 1);
            return winrt::hstring{ channels == L"2.0" ? std::wstring{ L"Stereo" } : L"Surround " + channels };
        }
        return L"Stereo";
    }

    struct LanguageTag final
    {
        wchar_t const* Tag;
        wchar_t const* Name;
    };

    constexpr LanguageTag LanguageTags[]{
        { L"ENG", L"English" }, { L"JPN", L"Japanese" }, { L"GER", L"German" }, { L"FRE", L"French" },
        { L"SPA", L"Spanish" }, { L"ITA", L"Italian" }, { L"KOR", L"Korean" }, { L"CHI", L"Chinese" },
        { L"HIN", L"Hindi" }, { L"RUS", L"Russian" }, { L"POR", L"Portuguese" }, { L"DUT", L"Dutch" },
        { L"NOR", L"Norwegian" }, { L"SWE", L"Swedish" }, { L"DAN", L"Danish" }, { L"FIN", L"Finnish" },
        { L"POL", L"Polish" }, { L"TUR", L"Turkish" }, { L"ARA", L"Arabic" }, { L"MULTI", L"several languages" },
        { L"DUAL", L"two languages" },
        // Addons are not consistent about which three-letter spelling they send.
        { L"DEU", L"German" }, { L"FRA", L"French" }, { L"ZHO", L"Chinese" }, { L"NLD", L"Dutch" },
        { L"CES", L"Czech" }, { L"CZE", L"Czech" }, { L"ELL", L"Greek" }, { L"GRE", L"Greek" },
        { L"RON", L"Romanian" }, { L"RUM", L"Romanian" }, { L"SLK", L"Slovak" }, { L"SLO", L"Slovak" },
        { L"EN", L"English" }, { L"JA", L"Japanese" }, { L"DE", L"German" }, { L"FR", L"French" },
        { L"ES", L"Spanish" }, { L"IT", L"Italian" }, { L"KO", L"Korean" }, { L"ZH", L"Chinese" },
        { L"HI", L"Hindi" }, { L"RU", L"Russian" }, { L"PT", L"Portuguese" }, { L"NL", L"Dutch" },
    };
}

namespace HaloDesktop::Sources
{
    QualityTier TierOf(std::wstring_view quality) noexcept
    {
        if (quality == L"2160p") return QualityTier::UltraHd;
        if (quality == L"1440p" || quality == L"1080p") return QualityTier::FullHd;
        if (quality == L"720p") return QualityTier::Hd;
        return QualityTier::Lower;
    }

    winrt::hstring TierLabel(QualityTier tier)
    {
        switch (tier)
        {
        case QualityTier::UltraHd: return L"4K";
        case QualityTier::FullHd: return L"Full HD";
        case QualityTier::Hd: return L"HD";
        case QualityTier::Lower: break;
        }
        return L"Lower";
    }

    StartSpeed SpeedOf(SourceStatus status) noexcept
    {
        switch (status)
        {
        case SourceStatus::Instant: return StartSpeed::Immediate;
        case SourceStatus::Caching: return StartSpeed::ShortWait;
        case SourceStatus::Uncached:
        case SourceStatus::Unknown:
            break;
        }
        return StartSpeed::NeedsDownload;
    }

    winrt::hstring StatusLabel(SourceStatus status)
    {
        switch (status)
        {
        case SourceStatus::Instant: return L"Plays instantly";
        case SourceStatus::Caching: return L"Ready in about a minute";
        case SourceStatus::Uncached: return L"Downloads before it plays";
        // Not the same report as "not cached", but the same wait for the viewer.
        case SourceStatus::Unknown: break;
        }
        return L"May download before it plays";
    }

    bool IsHdr(std::wstring_view range) noexcept
    {
        return !range.empty() && range != L"SDR";
    }

    winrt::hstring LanguageName(winrt::hstring const& code)
    {
        auto const upper = Upper(View(code));
        for (auto const& tag : LanguageTags)
        {
            if (upper == tag.Tag) return tag.Name;
        }
        return code;
    }

    ViewModels::Medal MedalOf(SourceEntry const& entry)
    {
        return ViewModels::MedalFor(View(entry.Quality), View(entry.Range));
    }

    winrt::hstring BitrateWarning(SourceEntry const& entry, double lineMbps)
    {
        if (entry.NeededMbps <= 0.0 || lineMbps <= 0.0) return {};
        if (entry.NeededMbps <= lineMbps) return {};
        return L"Needs " + Whole(entry.NeededMbps) + L" Mbps, more than your line";
    }

    winrt::hstring MetaLine(SourceEntry const& entry)
    {
        std::wstring line;
        for (auto const* part : { &entry.Codec, &entry.Audio, &entry.Size })
        {
            if (part->empty() || View(*part) == NotParsed) continue;
            if (!line.empty()) line.append(Separator);
            line.append(Upper(View(*part)));
        }
        return winrt::hstring{ line };
    }

    SourceDetails DetailsFor(SourceEntry const& entry, DeviceContext const& device)
    {
        auto const hdr = !entry.Range.empty() && entry.Range != L"SDR" && View(entry.Range) != NotParsed
            ? RangeDetail(entry.Range)
            : winrt::hstring{};
        std::vector<winrt::hstring> languages;
        for (auto const& token : SplitTokens(entry.Languages))
        {
            if (View(token) != NotParsed) languages.push_back(LanguageName(token));
        }
        std::vector<winrt::hstring> subtitles;
        for (auto const& language : entry.SubtitleLanguages) subtitles.push_back(LanguageName(language));

        SourceDetails result;
        auto const add = [&result](wchar_t const* label, std::vector<winrt::hstring> const& parts)
        {
            auto value = JoinList(parts, Separator);
            if (!value.empty()) result.Rows.push_back({ label, winrt::hstring{ value } });
        };
        add(L"Picture", { Dimensions(entry.Tier, View(entry.Quality)), hdr, CodecName(entry.Codec) });
        add(L"Sound", { SoundDetail(entry.Audio), winrt::hstring{ JoinList(languages, L", ") } });
        // Subtitles are the one fact whose absence is worth saying: it decides a pick.
        add(L"Subtitles", { subtitles.empty() ? winrt::hstring{ L"None" } : winrt::hstring{ JoinList(subtitles, L", ") } });

        result.Provider = entry.Provider.empty() ? winrt::hstring{ L"An addon" } : entry.Provider;
        switch (entry.Status)
        {
        case SourceStatus::Instant:
            result.CacheLabel = L"Cached";
            result.CacheGood = true;
            break;
        case SourceStatus::Caching:
            result.CacheLabel = L"Caching now";
            break;
        case SourceStatus::Uncached:
            result.CacheLabel = L"Not cached";
            break;
        case SourceStatus::Unknown:
            break;
        }

        if (entry.NeededMbps > 0.0)
        {
            auto const line = device.LineMbps;
            BandwidthDetail bandwidth;
            bandwidth.Label = Whole(entry.NeededMbps) + L" Mbps";
            if (line > 0.0)
            {
                bandwidth.Note = Whole(entry.NeededMbps / line * 100.0) + L"% of your " + Whole(line) + L" Mbps line";
                // Measured against the viewer's own line, the only scale that decides anything.
                bandwidth.Fraction = (std::min)(1.0, entry.NeededMbps / line);
            }
            result.Bandwidth = std::move(bandwidth);
        }
        return result;
    }

    winrt::hstring CountLabel(std::size_t count, wchar_t const* singular, wchar_t const* plural)
    {
        return winrt::to_hstring(count) + L" " + (count == 1 ? singular : plural);
    }
}
