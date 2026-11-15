#include "Playback/PlaybackPolicy.h"

#include "Security/ProtectedHttpHeaders.h"
#include "Services/LanguagePolicy.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>

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
    // Kept to its original short table on purpose: its only reader in the app
    // is the subtitle fingerprint, which is stored in the remembered subtitle
    // choices, so widening it would orphan every choice already saved.
    // Matching and naming use the full tables in Services/LanguagePolicy.
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
        return !left.empty() && !right.empty() && ::HaloDesktop::Services::LanguageTagsMatch(left, right);
    }

    std::wstring LanguageDisplayName(std::wstring const&code)
    {
        return ::HaloDesktop::Services::LanguageNameForTag(code).value_or(code);
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

    std::optional<double> ResumePosition(std::optional<SavedPlace> const& saved, double durationSeconds) noexcept
    {
        // A removal from the history is a removal of the place too.
        if (!saved || saved->Watched || saved->Removed) return std::nullopt;
        auto const position = saved->PositionSeconds;
        if (!std::isfinite(position) || position <= 0.0) return std::nullopt;
        if (std::isfinite(durationSeconds) && durationSeconds > 0.0 && position >= durationSeconds) return std::nullopt;
        return position;
    }

    bool CanApplyResumeNow(
        double currentPositionSeconds,
        std::uint64_t currentSeekSerial,
        std::uint64_t initialSeekSerial,
        bool withinStartupWindow) noexcept
    {
        return withinStartupWindow && currentSeekSerial == initialSeekSerial && currentPositionSeconds <= 5.0;
    }

    std::wstring TrackDisplayTitle(std::wstring const& title, std::wstring const& language, std::int64_t id)
    {
        // A blank title is no title: some files name their tracks with empty strings.
        auto const first = std::find_if_not(title.begin(), title.end(), [](wchar_t character)
        {
            return std::iswspace(character) != 0;
        });
        auto const last = std::find_if_not(title.rbegin(), title.rend(), [](wchar_t character)
        {
            return std::iswspace(character) != 0;
        }).base();
        if (first < last) return std::wstring(first, last);
        if (!language.empty())
        {
            auto label = LanguageDisplayName(language);
            if (!label.empty()) return label;
        }
        return L"Track " + std::to_wstring(id);
    }

    SubtitleFormat SubtitleFormatOf(std::wstring const& codec)
    {
        std::wstring lower;
        lower.reserve(codec.size());
        for (auto const character : codec)
        {
            lower.push_back(character >= L'A' && character <= L'Z' ? static_cast<wchar_t>(character - L'A' + L'a') : character);
        }
        if (lower == L"ass" || lower == L"ssa" || lower == L"ass-text") return SubtitleFormat::Styled;
        if (lower == L"subrip" || lower == L"srt" || lower == L"webvtt" || lower == L"vtt" || lower == L"text")
        {
            return SubtitleFormat::Text;
        }
        if (lower == L"hdmv_pgs_subtitle" || lower == L"pgssub" || lower == L"pgs") return SubtitleFormat::Bitmap;
        return SubtitleFormat::Unknown;
    }

    SubtitleControlAvailability SubtitleControlsFor(SubtitleFormat format, bool keepTrackStyling) noexcept
    {
        auto const bitmap = format == SubtitleFormat::Bitmap;
        return {
            .TrackStyling = format != SubtitleFormat::Bitmap && format != SubtitleFormat::Text,
            .Looks = !bitmap && !(format == SubtitleFormat::Styled && keepTrackStyling),
        };
    }

    std::wstring TrackStylingNote(SubtitleFormat format, bool keepTrackStyling)
    {
        switch (format)
        {
        case SubtitleFormat::Styled:
            return keepTrackStyling
                ? L"Keeping the track's own fonts and positions; only size applies."
                : L"Overriding the track with the settings here.";
        case SubtitleFormat::Text:
            return L"Plain text track: the settings here always apply.";
        case SubtitleFormat::Bitmap:
            return L"PGS is rendered images: font, outline and shadow do not apply.";
        case SubtitleFormat::Unknown:
        default:
            return L"On, a styled track keeps its own look. A plain text track always follows the settings here.";
        }
    }

    std::wstring SubtitleSizeNote(SubtitleFormat format)
    {
        return format == SubtitleFormat::Bitmap ? L"Bitmap track, scaled but not restyled" : L"mpv sub-scale, live";
    }

    bool IsSeekableDuration(double durationSeconds) noexcept
    {
        return std::isfinite(durationSeconds) && durationSeconds > 0.0;
    }

    EndOfVideoAction ResolveEndOfVideo(EndOfVideoContext const& context) noexcept
    {
        if (context.Autoplay && context.PlayingDownload && context.NextDownloadReady) return EndOfVideoAction::PlayNextDownload;
        if (context.Autoplay && context.NextEpisodeKnown && context.NextHasStream) return EndOfVideoAction::ShowUpNext;
        if (context.Autoplay && context.NextEpisodeKnown) return EndOfVideoAction::OpenNextSources;
        return EndOfVideoAction::ClosePlayer;
    }

    UpNextCancelAction ResolveUpNextCancel(bool endReached) noexcept
    {
        return endReached ? UpNextCancelAction::Exit : UpNextCancelAction::Dismiss;
    }

    std::wstring NextEpisodeTitle(std::wstring const& ownTitle, std::wstring const& showName, std::wstring const& currentTitle)
    {
        if (!ownTitle.empty()) return ownTitle;
        if (!showName.empty()) return showName;
        return currentTitle;
    }

    VideoFitMode VideoFitForPreference(bool fillPicture) noexcept
    {
        return fillPicture ? VideoFitMode::Fill : VideoFitMode::Fit;
    }

    bool AcceptEndOfFileSignal(bool& handled, EndOfFileSignal signal) noexcept
    {
        switch (signal)
        {
        case EndOfFileSignal::EofReached:
        case EndOfFileSignal::EndFileEof:
            if (handled) return false;
            handled = true;
            return true;
        case EndOfFileSignal::EofCleared:
        case EndOfFileSignal::FileLoaded:
        default:
            handled = false;
            return false;
        }
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

    bool ShouldReportPlaybackChange(
        bool endChanged,
        bool wasPaused,
        bool isPaused,
        PlaybackEndReason endReason) noexcept
    {
        // Cache starvation is transient, not a viewer decision to stop. An end
        // event owns its report, so a later pause property from the same terminal
        // transition cannot send the same position again.
        return endChanged
            || (!wasPaused && isPaused && endReason == PlaybackEndReason::None);
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
