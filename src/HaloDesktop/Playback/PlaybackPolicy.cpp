#include "Playback/PlaybackPolicy.h"

#include "Security/ProtectedHttpHeaders.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace
{
    constexpr std::wstring_view ExternalSubtitleMarker=L"halo-subtitle:";
    constexpr std::chrono::milliseconds BufferingIndicatorShowDelay{ 350 };
    constexpr std::chrono::milliseconds BufferingIndicatorMinimumVisible{ 500 };

    std::wstring NormalizeFingerprintComponent(std::wstring value)
    {
        std::wstring result;
        result.reserve(value.size());
        bool pendingSpace{};
        for (auto const character : value)
        {
            if (std::iswspace(character) != 0)
            {
                pendingSpace = !result.empty();
                continue;
            }
            if (pendingSpace)
            {
                result.push_back(L' ');
                pendingSpace = false;
            }
            result.push_back(static_cast<wchar_t>(std::towlower(character)));
        }
        return result;
    }

    void AppendFingerprintComponent(std::wstring& output, std::wstring const& value)
    {
        output += std::to_wstring(value.size());
        output.push_back(L':');
        output += value;
    }

    struct VideoQualityTier final
    {
        std::int64_t MinimumPixels;
        wchar_t const* Token;
        wchar_t const* Detail;
    };

    // Frame area rather than either dimension on its own. Scope framing keeps a
    // master's width while losing a third of its height, portrait video swaps the
    // two, and an ultrawide panel's width overstates its tier; area places all
    // three where a viewer would. The floors sit between the neighbouring formats
    // rather than on them, so a slightly cropped master keeps its tier.
    constexpr VideoQualityTier VideoQualityTiers[]{
        { 5'500'000, L"4K", L"ULTRA HD" },
        { 3'000'000, L"1440P", L"QHD" },
        { 1'400'000, L"1080P", L"FULL HD" },
        { 600'000, L"720P", L"HD" },
    };

    void AppendEscapedListValue(std::wstring& output, std::wstring const& value)
    {
        for (auto const character : value)
        {
            if (character == L'\\' || character == L',')
            {
                output.push_back(L'\\');
            }
            output.push_back(character);
        }
    }
}

namespace HaloDesktop::Playback
{
    std::wstring NormalizeLanguage(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        auto const separator=value.find_first_of(L"-_");
        if(separator!=std::wstring::npos)value.resize(separator);
        if (value == L"en") return L"eng";
        if (value == L"ja") return L"jpn";
        if (value == L"es") return L"spa";
        if (value == L"fr") return L"fra";
        if (value == L"de") return L"deu";
        if (value == L"pt") return L"por";
        if (value == L"it") return L"ita";
        if (value == L"ru") return L"rus";
        if (value == L"zh") return L"zho";
        if (value == L"fre") return L"fra";
        if (value == L"ger") return L"deu";
        if (value == L"chi") return L"zho";
        if (value == L"cze") return L"ces";
        if (value == L"dut") return L"nld";
        if (value == L"gre") return L"ell";
        if (value == L"rum") return L"ron";
        if (value == L"slo") return L"slk";
        return value;
    }

    bool LanguageMatches(std::wstring const& left, std::wstring const& right)
    {
        return !left.empty() && !right.empty() && NormalizeLanguage(left) == NormalizeLanguage(right);
    }

    std::wstring LanguageDisplayName(std::wstring const&code)
    {
        static std::unordered_map<std::wstring,wchar_t const*>const labels{
            {L"eng",L"English"},{L"jpn",L"Japanese"},{L"spa",L"Spanish"},{L"fra",L"French"},
            {L"deu",L"German"},{L"por",L"Portuguese"},{L"pob",L"Portuguese (BR)"},{L"ita",L"Italian"},
            {L"rus",L"Russian"},{L"zho",L"Chinese"}};
        auto const found=labels.find(NormalizeLanguage(code));
        return found==labels.end()?code:std::wstring{found->second};
    }

    std::optional<std::int64_t> FindLanguageTrack(
        std::vector<TrackInfo> const& tracks,
        TrackType type,
        std::wstring const& language,
        bool embeddedOnly)
    {
        auto const found = std::find_if(tracks.begin(), tracks.end(), [type,embeddedOnly,&language](TrackInfo const& track)
        {
            return track.Type == type
                && (!embeddedOnly || !track.External)
                && LanguageMatches(track.Language, language);
        });
        return found == tracks.end() ? std::nullopt : std::optional<std::int64_t>{ found->Id };
    }

    std::wstring TrackSummary(std::vector<TrackInfo> const& tracks, TrackType type)
    {
        auto const found = std::find_if(tracks.begin(), tracks.end(), [type](TrackInfo const& track)
        {
            return track.Type == type && track.Selected;
        });
        if (found == tracks.end())
        {
            return type == TrackType::Subtitle ? L"Off" : L"Automatic";
        }

        auto result = found->Title;
        if (result.empty())
        {
            result = found->Language;
        }
        if (result.empty())
        {
            result = type == TrackType::Subtitle ? L"Subtitle" : L"Audio";
        }
        if (!found->Codec.empty())
        {
            result += L" \x00B7 " + found->Codec;
        }
        return result;
    }

    VideoQualityBadge ClassifyVideoQuality(VideoFormat const& format)
    {
        if (format.Width <= 0 || format.Height <= 0)
        {
            return {};
        }

        auto const pixels = static_cast<std::int64_t>(format.Width) * static_cast<std::int64_t>(format.Height);
        VideoQualityBadge badge{ L"SD", {} };
        for (auto const& tier : VideoQualityTiers)
        {
            if (pixels >= tier.MinimumPixels)
            {
                badge = { tier.Token, tier.Detail };
                break;
            }
        }
        // Dynamic range replaces the resolution qualifier rather than joining it: it
        // is the rarer fact, and the tier token already carries the resolution.
        switch (format.DynamicRange)
        {
        case VideoDynamicRange::Hdr:
            badge.Detail = L"HDR";
            break;
        case VideoDynamicRange::Hlg:
            badge.Detail = L"HLG";
            break;
        case VideoDynamicRange::DolbyVision:
            badge.Detail = L"DOLBY VISION";
            break;
        case VideoDynamicRange::Standard:
            break;
        }
        return badge;
    }

    std::wstring EncodeExternalSubtitleTrackTitle(std::wstring const&identity,std::wstring const&displayTitle)
    {
        return std::wstring{ExternalSubtitleMarker}+std::to_wstring(identity.size())+L":"+identity+displayTitle;
    }

    std::optional<std::pair<std::wstring,std::wstring>> DecodeExternalSubtitleTrackTitle(std::wstring const&encoded)
    {
        if(!encoded.starts_with(ExternalSubtitleMarker))return std::nullopt;
        auto const lengthStart=ExternalSubtitleMarker.size();auto const separator=encoded.find(L':',lengthStart);
        if(separator==std::wstring::npos||separator==lengthStart)return std::nullopt;
        std::size_t length{};
        for(auto index=lengthStart;index<separator;++index)
        {
            auto const character=encoded[index];if(character<L'0'||character>L'9')return std::nullopt;
            auto const digit=static_cast<std::size_t>(character-L'0');
            if(length>((std::numeric_limits<std::size_t>::max)()-digit)/10)return std::nullopt;
            length=length*10+digit;
        }
        auto const identityStart=separator+1;if(length>encoded.size()-identityStart)return std::nullopt;
        return std::pair{encoded.substr(identityStart,length),encoded.substr(identityStart+length)};
    }

    bool CanApplyAutomaticSelection(std::uint64_t currentSelectionSerial,std::uint64_t initialSelectionSerial,std::uint64_t automaticSelectionSerial)noexcept
    {
        return currentSelectionSerial==initialSelectionSerial||currentSelectionSerial==automaticSelectionSerial;
    }

    bool ShouldApplyResume(
        bool resumeEnabled,
        bool watched,
        double positionSeconds,
        double durationSeconds,
        double currentPositionSeconds,
        std::uint64_t currentSeekSerial,
        std::uint64_t initialSeekSerial,
        bool withinStartupWindow) noexcept
    {
        if (!resumeEnabled || watched || positionSeconds <= 30 || durationSeconds <= 0.0
            || currentPositionSeconds > 5.0 || currentSeekSerial != initialSeekSerial || !withinStartupWindow)
        {
            return false;
        }
        return positionSeconds / durationSeconds < 0.95;
    }

    bool ResolveBufferingState(
        bool current,
        std::optional<bool> pausedForCache,
        bool playbackReady,
        bool pausedForCacheActive)noexcept
    {
        if(pausedForCache)return *pausedForCache;
        return playbackReady&&!pausedForCacheActive?false:current;
    }

    bool IsPlaybackStalled(bool buffering,bool seekPending,bool paused)noexcept
    {
        return (buffering||seekPending)&&!paused;
    }

    std::chrono::milliseconds BufferingIndicatorDelay(bool firstFrameReady)noexcept
    {
        return firstFrameReady?BufferingIndicatorShowDelay:std::chrono::milliseconds::zero();
    }

    std::chrono::milliseconds BufferingIndicatorHoldRemaining(std::chrono::milliseconds shownFor)noexcept
    {
        if(shownFor>=BufferingIndicatorMinimumVisible)return std::chrono::milliseconds::zero();
        return BufferingIndicatorMinimumVisible-shownFor;
    }

    bool ShouldReportPlaybackChange(bool endChanged,bool wasPlaying,bool isPlaying)noexcept
    {
        return endChanged||(wasPlaying&&!isPlaying);
    }

    bool ShouldExitMpvEventLoop(bool stopping,bool shutdownEvent)noexcept
    {
        return stopping||shutdownEvent;
    }

    PlaybackTimeline NormalizePlaybackTimeline(
        double positionSeconds,
        double durationSeconds) noexcept
    {
        if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0)
        {
            return {};
        }
        if (!std::isfinite(positionSeconds))
        {
            positionSeconds = 0.0;
        }
        return {
            std::clamp(positionSeconds, 0.0, durationSeconds),
            durationSeconds,
        };
    }

    bool IsPlaybackSpeedSelected(double actual,double choice)noexcept
    {
        return std::abs(actual-choice)<0.001;
    }

    std::int32_t AdjustPlaybackDelayMilliseconds(std::int32_t current,std::int32_t delta)noexcept
    {
        return std::clamp(current+delta,-5000,5000);
    }

    std::wstring SubtitleTrackFingerprint(TrackInfo const&track)
    {
        auto const language=NormalizeLanguage(track.Language);
        auto const title=NormalizeFingerprintComponent(track.Title);
        auto const codec=NormalizeFingerprintComponent(track.Codec);
        std::wstring result;
        result.reserve(language.size()+title.size()+codec.size()+32);
        AppendFingerprintComponent(result,language);
        AppendFingerprintComponent(result,title);
        AppendFingerprintComponent(result,codec);
        return result;
    }

    std::optional<std::int64_t> FindEmbeddedSubtitleByFingerprint(
        std::vector<TrackInfo>const&tracks,std::wstring const&fingerprint)
    {
        auto const found=std::find_if(tracks.begin(),tracks.end(),[&fingerprint](TrackInfo const&track)
        {
            return track.Type==TrackType::Subtitle&&!track.External
                && SubtitleTrackFingerprint(track)==fingerprint;
        });
        return found==tracks.end()?std::nullopt:std::optional<std::int64_t>{found->Id};
    }

    SubtitleIntentResolution ResolveSubtitleIntent(
        SubtitleIntentKind intent,
        bool exactVideo) noexcept
    {
        switch(intent)
        {
        case SubtitleIntentKind::Off:
            return exactVideo
                ? SubtitleIntentResolution::Disable
                : SubtitleIntentResolution::GlobalPreference;
        case SubtitleIntentKind::Embedded:
            return exactVideo
                ? SubtitleIntentResolution::ExactOnly
                : SubtitleIntentResolution::LanguageFallback;
        case SubtitleIntentKind::Addon:
            return exactVideo
                ? SubtitleIntentResolution::ExactThenLanguage
                : SubtitleIntentResolution::LanguageFallback;
        case SubtitleIntentKind::Automatic:
        default:
            return SubtitleIntentResolution::GlobalPreference;
        }
    }

    std::wstring SerializePlaybackHeaders(std::vector<PlaybackHeader> const& headers)
    {
        Security::ValidateProtectedHttpHeaders(headers);
        std::wstring result;
        for (auto const& header : headers)
        {
            if (!result.empty())
            {
                result.push_back(L',');
            }
            AppendEscapedListValue(result, header.Name + L": " + header.Value);
        }
        return result;
    }
    std::wstring FormatPlaybackTime(double seconds, bool withHours)
    {
        auto const totalSeconds = static_cast<std::int32_t>((std::max)(0.0, seconds));
        std::wostringstream value;
        if (withHours)
        {
            value << totalSeconds / 3600 << L":" << std::setw(2) << std::setfill(L'0')
                  << totalSeconds % 3600 / 60;
        }
        else
        {
            value << totalSeconds / 60;
        }
        value << L":" << std::setw(2) << std::setfill(L'0') << totalSeconds % 60;
        return value.str();
    }
}
