#include "pch.h"
#include "ViewModels/SourcePresentation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using HaloDesktop::Sources::QualityTier;
    using HaloDesktop::Sources::SourceEntry;
    using HaloDesktop::Sources::StartSpeed;

    constexpr wchar_t const* Separator = L" \x00B7 ";

    // The parser leaves anything it could not identify as this literal, and the
    // sheet has to say "not listed" rather than invent a value for it.
    constexpr wchar_t const* NotParsed = L"UNKNOWN";

    std::wstring Upper(winrt::hstring const& value)
    {
        std::wstring result{ value.c_str() };
        std::transform(result.begin(), result.end(), result.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towupper(character));
        });
        return result;
    }

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

    // "English, German and Spanish" rather than a bare comma list: the language
    // line is read as a sentence, not as a data cell.
    winrt::hstring JoinProse(std::vector<winrt::hstring> const& values)
    {
        std::wstring result;
        for (std::size_t index{}; index < values.size(); ++index)
        {
            if (index > 0) result.append(index + 1 == values.size() ? L" and " : L", ");
            result.append(values[index].c_str());
        }
        return winrt::hstring{ result };
    }

    winrt::hstring JoinList(std::vector<winrt::hstring> const& values)
    {
        std::wstring result;
        for (auto const& value : values)
        {
            if (!result.empty()) result.append(L", ");
            result.append(value.c_str());
        }
        return winrt::hstring{ result };
    }

    winrt::hstring Whole(double value)
    {
        return winrt::to_hstring(static_cast<std::int64_t>(std::llround(value)));
    }

    winrt::hstring Clock(double seconds)
    {
        auto const total = static_cast<std::int64_t>(seconds < 0 ? 0 : seconds);
        std::wostringstream output;
        auto const hours = total / 3600;
        if (hours > 0) output << hours << L':';
        output << std::setw(2) << std::setfill(L'0') << (total % 3600) / 60
               << L':' << std::setw(2) << std::setfill(L'0') << total % 60;
        return winrt::hstring{ output.str() };
    }

    // Picture dimensions are only ever shown inside the expander, where the
    // viewer has explicitly asked for the technical read-out.
    wchar_t const* Dimensions(QualityTier tier, winrt::hstring const& quality)
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
        return nullptr;
    }

    winrt::hstring CodecName(winrt::hstring const& codec)
    {
        if (codec.empty() || codec == NotParsed) return {};
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
        if (audio.empty() || audio == NotParsed) return L"Not listed";
        auto const text = Upper(audio);
        if (text == L"ATMOS") return L"Dolby Atmos";
        if (text == L"TRUEHD") return L"Dolby TrueHD";
        if (text == L"DTS-HD") return L"DTS-HD";
        if (text == L"DTS") return L"DTS";
        // "DDP 5.1" and friends carry the channel layout, which is the part worth
        // repeating; the codec initials are not something to put in front of anyone.
        auto const space = text.find(L' ');
        if (space != std::wstring::npos)
        {
            auto const channels = text.substr(space + 1);
            return winrt::hstring{ (channels == L"2.0" ? std::wstring{ L"Stereo" } : L"Surround " + channels) };
        }
        return L"Stereo";
    }
}

namespace HaloDesktop::Sources
{
    QualityTier TierOf(winrt::hstring const& quality) noexcept
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

    winrt::hstring BadgeTierLabel(QualityTier tier)
    {
        return winrt::hstring{ Upper(TierLabel(tier)) };
    }

    bool IsPremiumTier(QualityTier tier) noexcept
    {
        return tier == QualityTier::UltraHd;
    }

    StartSpeed SpeedOf(winrt::HaloDesktop::StreamStatus status) noexcept
    {
        switch (status)
        {
        case winrt::HaloDesktop::StreamStatus::Instant:
        case winrt::HaloDesktop::StreamStatus::OnDisk:
            return StartSpeed::Immediate;
        case winrt::HaloDesktop::StreamStatus::Caching:
            return StartSpeed::ShortWait;
        case winrt::HaloDesktop::StreamStatus::Uncached:
        case winrt::HaloDesktop::StreamStatus::Unknown:
            break;
        }
        return StartSpeed::NeedsDownload;
    }

    winrt::hstring StatusLabel(winrt::HaloDesktop::StreamStatus status)
    {
        switch (status)
        {
        case winrt::HaloDesktop::StreamStatus::Instant: return L"Plays instantly";
        case winrt::HaloDesktop::StreamStatus::OnDisk: return L"Already on this device";
        case winrt::HaloDesktop::StreamStatus::Caching: return L"Ready in about a minute";
        case winrt::HaloDesktop::StreamStatus::Uncached: return L"Downloads before it plays";
        // An addon that reported nothing about caching is not the same as one that
        // reported "not cached", but the wait it implies for the viewer is.
        case winrt::HaloDesktop::StreamStatus::Unknown: return L"May download before it plays";
        }
        return L"May download before it plays";
    }

    bool IsHdr(winrt::hstring const& range) noexcept
    {
        return !range.empty() && range != L"SDR";
    }

    winrt::hstring RangeLabel(winrt::hstring const& range)
    {
        return IsHdr(range) ? winrt::hstring{ L"HDR" } : winrt::hstring{ L"Standard range" };
    }

    bool IsSurround(winrt::hstring const& audio) noexcept
    {
        auto const text = Upper(audio);
        if (text == L"ATMOS" || text == L"TRUEHD" || text == L"DTS-HD" || text == L"DTS") return true;
        // Everything else that carries a layout writes it as "N.M"; five channels
        // in front of the point is the line between stereo and surround.
        auto const point = text.find(L'.');
        if (point == std::wstring::npos || point == 0) return false;
        auto const leading = text[point - 1];
        return std::iswdigit(leading) && leading >= L'5';
    }

    winrt::hstring SoundLabel(winrt::hstring const& audio)
    {
        if (audio.empty() || audio == NotParsed) return L"Sound not listed";
        return IsSurround(audio) ? winrt::hstring{ L"Surround sound" } : winrt::hstring{ L"Stereo sound" };
    }

    winrt::hstring SizeLabel(winrt::hstring const& size)
    {
        return size.empty() || size == NotParsed ? winrt::hstring{ L"size not listed" } : size;
    }

    winrt::hstring LanguageName(winrt::hstring const& code)
    {
        static constexpr std::pair<wchar_t const*, wchar_t const*> Names[]{
            { L"ENG", L"English" }, { L"JPN", L"Japanese" }, { L"GER", L"German" },
            { L"FRE", L"French" }, { L"SPA", L"Spanish" }, { L"ITA", L"Italian" },
            { L"KOR", L"Korean" }, { L"CHI", L"Chinese" }, { L"HIN", L"Hindi" },
            { L"RUS", L"Russian" }, { L"POR", L"Portuguese" }, { L"DUT", L"Dutch" },
            { L"NOR", L"Norwegian" }, { L"SWE", L"Swedish" }, { L"DAN", L"Danish" },
            { L"FIN", L"Finnish" }, { L"POL", L"Polish" }, { L"TUR", L"Turkish" },
            { L"ARA", L"Arabic" }, { L"MULTI", L"several languages" },
            { L"DUAL", L"two languages" },
            // The three-letter tags addons use are not consistent, so the common
            // bibliographic spellings map onto the same names as the terminology ones.
            { L"DEU", L"German" }, { L"FRA", L"French" }, { L"ZHO", L"Chinese" },
            { L"NLD", L"Dutch" }, { L"CES", L"Czech" }, { L"CZE", L"Czech" },
            { L"ELL", L"Greek" }, { L"GRE", L"Greek" }, { L"RON", L"Romanian" },
            { L"RUM", L"Romanian" }, { L"SLK", L"Slovak" }, { L"SLO", L"Slovak" },
            { L"EN", L"English" }, { L"JA", L"Japanese" }, { L"DE", L"German" },
            { L"FR", L"French" }, { L"ES", L"Spanish" }, { L"IT", L"Italian" },
            { L"KO", L"Korean" }, { L"ZH", L"Chinese" }, { L"HI", L"Hindi" },
            { L"RU", L"Russian" }, { L"PT", L"Portuguese" }, { L"NL", L"Dutch" },
        };
        auto const upper = Upper(code);
        for (auto const& [tag, name] : Names)
        {
            if (upper == tag) return name;
        }
        return code;
    }

    winrt::hstring AudioLanguageLine(winrt::hstring const& languages)
    {
        if (languages.empty() || languages == NotParsed) return L"Audio language not listed";
        std::vector<winrt::hstring> names;
        for (auto const& token : SplitTokens(languages)) names.push_back(LanguageName(token));
        if (names.empty()) return L"Audio language not listed";
        return JoinProse(names) + L" audio";
    }

    winrt::hstring SubtitleStatement(
        winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> const& tracks,
        std::optional<winrt::hstring> const& preferred)
    {
        if (!tracks || tracks.Size() == 0) return L"no subtitles included";
        if (!preferred || preferred->empty())
        {
            return tracks.Size() == 1
                ? winrt::hstring{ L"1 subtitle track" }
                : winrt::to_hstring(tracks.Size()) + L" subtitle tracks";
        }
        auto const wanted = LanguageName(*preferred);
        auto const wantedUpper = Upper(*preferred);
        for (auto const& track : tracks)
        {
            if (Upper(track) == wantedUpper || LanguageName(track) == wanted)
            {
                return wanted + L" subtitles included";
            }
        }
        return L"no " + wanted + L" subtitles";
    }

    SourceEntry MakeEntry(
        winrt::HaloDesktop::StreamSource const& source,
        winrt::hstring const& providerId,
        winrt::hstring const& provider,
        double durationSeconds)
    {
        SourceEntry entry;
        entry.Source = source;
        entry.ProviderId = providerId;
        entry.Provider = provider;
        entry.Tier = TierOf(source.Quality());
        entry.Speed = SpeedOf(source.Status());
        entry.Hdr = IsHdr(source.Range());
        entry.Surround = IsSurround(source.Audio());
        entry.SizeBytes = source.SizeBytes();
        entry.Rank = source.Rank();
        if (entry.SizeBytes > 0 && durationSeconds > 0.0)
        {
            entry.NeededMbps = static_cast<double>(entry.SizeBytes) * 8.0 / durationSeconds / 1'000'000.0;
        }
        return entry;
    }

    winrt::hstring BitrateWarning(SourceEntry const& entry, double lineMbps)
    {
        // A file already sitting on the disk plays off local storage, so the line
        // it originally arrived over is no longer part of the question.
        if (entry.Source && entry.Source.Status() == winrt::HaloDesktop::StreamStatus::OnDisk) return {};
        if (entry.NeededMbps <= 0.0 || lineMbps <= 0.0) return {};
        if (entry.NeededMbps <= lineMbps) return {};
        return L"Needs " + Whole(entry.NeededMbps) + L" Mbps, more than your line";
    }

    winrt::hstring Ordinal(std::int32_t position)
    {
        auto const value = position < 1 ? 1 : position;
        auto const text = winrt::to_hstring(value);
        auto const lastTwo = value % 100;
        if (lastTwo >= 11 && lastTwo <= 13) return text + L"th";
        switch (value % 10)
        {
        case 1: return text + L"st";
        case 2: return text + L"nd";
        case 3: return text + L"rd";
        default: break;
        }
        return text + L"th";
    }

    winrt::hstring ReasonFor(SourceEntry const& entry, SourceEntry const* pick, bool isSmallest)
    {
        if (!entry.Source) return {};
        if (entry.Source.Status() == winrt::HaloDesktop::StreamStatus::OnDisk) return L"On your disk";
        if (pick && pick->Source && pick->Source.Key() == entry.Source.Key()) return L"Halo\x2019s pick";
        auto const ordinal = Ordinal(entry.Rank + 1);
        if (!pick) return ordinal;
        if (pick->Surround && !entry.Surround) return ordinal + L" \x00B7 stereo only";
        if (pick->Hdr && !entry.Hdr) return ordinal + L" \x00B7 no HDR";
        if (entry.Tier > pick->Tier) return ordinal + L" \x00B7 lower picture";
        if (isSmallest) return ordinal + L" \x00B7 smallest file";
        return ordinal;
    }

    winrt::hstring PickHeadline(SourceEntry const& entry, bool alone)
    {
        // Checked before the alone case, which would otherwise credit a provider for
        // a file that is on the device precisely because nobody had to answer.
        if (entry.Source && entry.Source.Status() == winrt::HaloDesktop::StreamStatus::OnDisk)
        {
            return L"Already saved here, so it starts with no connection at all.";
        }
        if (alone) return L"The only source that answered for this.";
        if (entry.Speed == StartSpeed::Immediate)
        {
            return entry.Tier == QualityTier::UltraHd || entry.Tier == QualityTier::FullHd
                ? winrt::hstring{ L"The best picture here that starts with no waiting." }
                : winrt::hstring{ L"The one that starts with no waiting." };
        }
        if (entry.Speed == StartSpeed::ShortWait) return L"The best picture here, once the cache catches up.";
        return L"The best picture here, but it has to come down first.";
    }

    winrt::hstring PickSummary(SourceEntry const& entry, DeviceContext const& device)
    {
        if (!entry.Source) return {};
        // "Sound not listed in English and Japanese" would be a sentence that says
        // the opposite of what it means, so the two clauses only join when the
        // sound is actually known.
        auto const sound = SoundLabel(entry.Source.Audio());
        auto const known = sound != L"Sound not listed";
        auto const languages = AudioLanguageLine(entry.Source.Languages());
        std::wstring spoken;
        if (languages != L"Audio language not listed")
        {
            spoken.assign(languages.c_str());
            // The pick reads as one sentence, so the trailing noun from the row
            // line ("... audio") is dropped here.
            auto const suffix = std::wstring{ L" audio" };
            if (spoken.size() > suffix.size() && spoken.compare(spoken.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                spoken.resize(spoken.size() - suffix.size());
            }
        }
        std::wstring result;
        if (known)
        {
            result.assign(sound.c_str());
            if (!spoken.empty()) result.append(L" in ").append(spoken);
        }
        else if (!spoken.empty())
        {
            result.assign(spoken).append(L" audio, sound format not listed");
        }
        else
        {
            result.assign(sound.c_str());
        }
        auto const subtitles = entry.Source.SubtitleLanguages();
        result.append(L", ");
        result.append(SubtitleStatement(subtitles, device.PreferredSubtitleLanguage).c_str());
        result.append(L", ");
        result.append(SizeLabel(entry.Source.Size()).c_str());
        result.push_back(L'.');
        if (!result.empty()) result[0] = static_cast<wchar_t>(std::towupper(result[0]));
        return winrt::hstring{ result };
    }

    bool HasWatchProgress(DeviceContext const& device) noexcept
    {
        if (device.WatchedDurationSeconds <= 0.0 || device.WatchedSeconds <= 0.0) return false;
        auto const fraction = device.WatchedSeconds / device.WatchedDurationSeconds;
        return fraction > 0.02 && fraction < 0.98;
    }

    winrt::hstring WatchNote(DeviceContext const& device)
    {
        if (!HasWatchProgress(device)) return {};
        auto const fraction = device.WatchedSeconds / device.WatchedDurationSeconds;
        return L"You watched " + Whole(fraction * 100.0) + L"% of this already";
    }

    std::vector<SpecRow> SpecsFor(SourceEntry const& entry, DeviceContext const& device)
    {
        std::vector<SpecRow> rows;
        if (!entry.Source) return rows;
        auto const& source = entry.Source;

        std::wstring picture;
        if (auto const* dimensions = Dimensions(entry.Tier, source.Quality())) picture.append(dimensions);
        if (!picture.empty()) picture.append(Separator);
        picture.append(entry.Hdr ? std::wstring{ RangeDetail(source.Range()).c_str() } : std::wstring{ L"standard range" });
        if (auto const codec = CodecName(source.Codec()); !codec.empty())
        {
            picture.append(Separator);
            picture.append(codec.c_str());
        }
        rows.emplace_back(L"Picture", winrt::hstring{ picture });
        rows.emplace_back(L"Sound", SoundDetail(source.Audio()));

        std::vector<winrt::hstring> spoken;
        for (auto const& token : SplitTokens(source.Languages()))
        {
            if (token == NotParsed) continue;
            spoken.push_back(LanguageName(token));
        }
        rows.emplace_back(L"Audio languages", spoken.empty() ? winrt::hstring{ L"Not listed" } : JoinList(spoken));

        auto const tracks = source.SubtitleLanguages();
        if (!tracks || tracks.Size() == 0)
        {
            rows.emplace_back(L"Subtitles", L"None included");
        }
        else
        {
            std::vector<winrt::hstring> names;
            for (auto const& track : tracks)
            {
                auto const name = LanguageName(track);
                if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
                if (names.size() == 4) break;
            }
            rows.emplace_back(
                L"Subtitles",
                CountLabel(tracks.Size(), L"track", L"tracks") + Separator + JoinList(names));
        }

        if (entry.NeededMbps > 0.0)
        {
            std::wstring needs{ Whole(entry.NeededMbps).c_str() };
            needs.append(L" Mbps");
            if (entry.Speed == StartSpeed::NeedsDownload)
            {
                needs.append(L" once downloaded");
            }
            else if (device.LineMbps > 0.0)
            {
                needs.append(Separator);
                needs.append(entry.NeededMbps > device.LineMbps
                    ? std::wstring{ L"more than your line measured" }
                    : L"your line measured " + std::wstring{ Whole(device.LineMbps).c_str() } + L" Mbps");
            }
            rows.emplace_back(L"Needs about", winrt::hstring{ needs });
        }

        if (HasWatchProgress(device))
        {
            rows.emplace_back(
                L"Watch progress",
                Clock(device.WatchedSeconds) + L" of " + Clock(device.WatchedDurationSeconds));
        }

        std::wstring origin{ entry.Provider.empty() ? std::wstring{ L"An addon" } : std::wstring{ entry.Provider.c_str() } };
        origin.append(Separator);
        switch (source.Status())
        {
        case winrt::HaloDesktop::StreamStatus::OnDisk: origin.append(L"already saved on this device"); break;
        case winrt::HaloDesktop::StreamStatus::Instant: origin.append(L"already cached for you"); break;
        case winrt::HaloDesktop::StreamStatus::Caching: origin.append(L"caching now"); break;
        case winrt::HaloDesktop::StreamStatus::Uncached: origin.append(L"not cached yet"); break;
        case winrt::HaloDesktop::StreamStatus::Unknown: origin.append(L"cache state not reported"); break;
        }
        rows.emplace_back(L"Comes from", winrt::hstring{ origin });
        return rows;
    }

    SourceDetailsData DetailsFor(
        SourceEntry const& entry,
        DeviceContext const& device,
        double maximumNeededMbps)
    {
        SourceDetailsData result;
        if (!entry.Source) return result;
        auto const& source = entry.Source;
        if (auto const* dimensions = Dimensions(entry.Tier, source.Quality()))
        {
            result.Resolution = dimensions;
        }
        else
        {
            result.Resolution = L"Resolution not listed";
        }
        // The detail tiles set every chip in the mono plate, which is drawn for
        // upper case; a title-cased label reads as a different control beside
        // "NONE INCLUDED" in the tile next to it.
        result.Picture = source.Range().empty() || source.Range() == L"SDR"
            ? winrt::hstring{ L"SDR" }
            : winrt::hstring{ Upper(RangeDetail(source.Range())) };
        result.Codec = CodecName(source.Codec());
        if (result.Codec.empty()) result.Codec = L"Codec not listed";
        result.Sound = SoundDetail(source.Audio());
        auto const audio = Upper(source.Audio());
        auto const separator = audio.find(L' ');
        result.Channels = separator == std::wstring::npos ? L"Channels not listed" : winrt::hstring{ audio.substr(separator + 1) };

        for (auto const& token : SplitTokens(source.Languages()))
        {
            if (token != NotParsed) result.AudioLanguages.push_back({ winrt::hstring{ Upper(LanguageName(token)) }, false });
        }
        // An empty tile reads as a rendering fault. Both lists say so instead,
        // muted, so an absence is distinguishable from something not loading.
        if (result.AudioLanguages.empty()) result.AudioLanguages.push_back({ L"NOT LISTED", true });
        for (auto const& token : source.SubtitleLanguages())
        {
            result.Subtitles.push_back({ winrt::hstring{ Upper(LanguageName(token)) }, false });
        }
        if (result.Subtitles.empty()) result.Subtitles.push_back({ L"NONE INCLUDED", true });
        result.Provider = entry.Provider.empty() ? L"An addon" : entry.Provider;
        auto const status = source.Status();
        switch (status)
        {
        case winrt::HaloDesktop::StreamStatus::OnDisk:
            result.CacheLabel = L"Saved on this device";
            result.CacheGood = true;
            break;
        case winrt::HaloDesktop::StreamStatus::Instant:
            result.CacheLabel = L"Already cached for you";
            result.CacheGood = true;
            break;
        case winrt::HaloDesktop::StreamStatus::Caching:
            result.CacheLabel = L"Caching now";
            break;
        case winrt::HaloDesktop::StreamStatus::Uncached:
            result.CacheLabel = L"Not cached yet";
            break;
        case winrt::HaloDesktop::StreamStatus::Unknown:
            result.CacheLabel = L"Cache state not reported";
            break;
        }
        auto const maximum = maximumNeededMbps > 0.0 ? maximumNeededMbps : entry.NeededMbps;
        result.LineLabel = maximum > 0.0
            ? L"HEAVIEST HERE " + Whole(maximum) + L" MBPS"
            : L"BANDWIDTH NOT MEASURED";
        if (entry.NeededMbps > 0.0)
        {
            result.MbpsLabel = Whole(entry.NeededMbps) + L" Mbps";
            if (device.LineMbps > 0.0)
            {
                result.Headroom = Whole(entry.NeededMbps / device.LineMbps * 100.0)
                    + L"% of your " + Whole(device.LineMbps) + L" Mbps line";
            }
            else
            {
                result.Headroom = L"Line speed has not been measured yet";
            }
            result.MeterFraction = maximum > 0.0 ? entry.NeededMbps / maximum : 0.0;
        }
        else
        {
            result.MbpsLabel = L"Not available";
            result.Headroom = L"File size or runtime is not listed";
        }
        return result;
    }

    winrt::hstring OutcomeGroupName(StartSpeed speed)
    {
        switch (speed)
        {
        case StartSpeed::Immediate: return L"Also ready now";
        case StartSpeed::ShortWait: return L"Short wait";
        case StartSpeed::NeedsDownload: break;
        }
        return L"Needs downloading first";
    }

    winrt::hstring OutcomeGroupNote(StartSpeed speed)
    {
        switch (speed)
        {
        case StartSpeed::Immediate: return L"Starts the instant you press play";
        case StartSpeed::ShortWait: return L"Your debrid is still pulling these in";
        case StartSpeed::NeedsDownload: break;
        }
        return L"Nobody has cached these yet";
    }

    winrt::hstring CountLabel(std::size_t count, wchar_t const* singular, wchar_t const* plural)
    {
        return winrt::to_hstring(count) + L" " + (count == 1 ? singular : plural);
    }
}
