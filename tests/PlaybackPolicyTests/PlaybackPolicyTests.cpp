#include "Api/OpenSubtitlesHashPolicy.h"
#include "Api/ResponseSizePolicy.h"
#include "Playback/PlaybackPolicy.h"
#include "Playback/ScrubPreviewPolicy.h"
#include "Playback/ScopedReentrancyGuard.h"
#include "Playback/TemporaryFileCollection.h"
#include "Security/ProtectedHttpHeaders.h"
#include "Services/AddonSelectionPolicy.h"
#include "Services/DownloadSourceMatch.h"
#include "Shell/WindowPresentationPolicy.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    void Require(bool condition,char const* message)
    {
        if(!condition)throw std::runtime_error(message);
    }

    void TestScrubPreviewMapping()
    {
        using HaloDesktop::Playback::ScrubPreviewTimeFromPointer;

        // The thumb centre travels between half a thumb width from each end, so the two
        // extremes of that travel must reach exactly the start and the end of the file.
        auto const start=ScrubPreviewTimeFromPointer(8.0,608.0,16.0,600.0);
        Require(start.Valid,"the left end of the thumb travel should map");
        Require(start.Seconds==0.0,"the left end of the thumb travel is the start of the file");
        auto const end=ScrubPreviewTimeFromPointer(600.0,608.0,16.0,600.0);
        Require(end.Valid,"the right end of the thumb travel should map");
        Require(end.Seconds==600.0,"the right end of the thumb travel is the end of the file");
        auto const middle=ScrubPreviewTimeFromPointer(304.0,608.0,16.0,600.0);
        Require(middle.Valid&&middle.Seconds==300.0,"the centre of the track is the middle of the file");

        // Pointer positions inside the thumb's own half-widths, and beyond the control
        // entirely, are clamped rather than producing a negative or overlong time.
        Require(ScrubPreviewTimeFromPointer(0.0,608.0,16.0,600.0).Seconds==0.0,"before the travel clamps to zero");
        Require(ScrubPreviewTimeFromPointer(-40.0,608.0,16.0,600.0).Seconds==0.0,"left of the control clamps to zero");
        Require(ScrubPreviewTimeFromPointer(608.0,608.0,16.0,600.0).Seconds==600.0,"after the travel clamps to the duration");
        Require(ScrubPreviewTimeFromPointer(5000.0,608.0,16.0,600.0).Seconds==600.0,"right of the control clamps to the duration");

        // Nothing to preview: a live stream with no duration, a bar that has not been
        // measured yet, and a track no wider than the thumb it carries.
        Require(!ScrubPreviewTimeFromPointer(100.0,608.0,16.0,0.0).Valid,"a file with no duration has no preview");
        Require(!ScrubPreviewTimeFromPointer(100.0,608.0,16.0,-1.0).Valid,"a negative duration has no preview");
        Require(!ScrubPreviewTimeFromPointer(100.0,0.0,16.0,600.0).Valid,"an unmeasured bar has no preview");
        Require(!ScrubPreviewTimeFromPointer(100.0,16.0,16.0,600.0).Valid,"a track the width of the thumb has no preview");
        Require(!ScrubPreviewTimeFromPointer(
            std::numeric_limits<double>::quiet_NaN(),608.0,16.0,600.0).Valid,"a pointer with no position has no preview");
    }

    void TestScrubPreviewPlacement()
    {
        using HaloDesktop::Playback::ClampScrubPreviewOffset;

        Require(ClampScrubPreviewOffset(400.0,200.0,800.0)==300.0,"the card centres on the pointer");
        Require(ClampScrubPreviewOffset(10.0,200.0,800.0)==0.0,"the card cannot overhang the left edge");
        Require(ClampScrubPreviewOffset(790.0,200.0,800.0)==600.0,"the card cannot overhang the right edge");
        Require(ClampScrubPreviewOffset(400.0,200.0,150.0)==0.0,"a card wider than its host pins to the left");
        Require(ClampScrubPreviewOffset(400.0,0.0,800.0)==0.0,"a card with no width has no offset");
    }

    void TestScrubPreviewCoalescing()
    {
        using HaloDesktop::Playback::ShouldIssueScrubPreview;
        using HaloDesktop::Playback::ScrubPreviewMinimumDeltaSeconds;

        Require(ShouldIssueScrubPreview(10.0,0.0,false),"the first request is always issued");
        Require(!ShouldIssueScrubPreview(10.0,10.0,true),"the same position is not decoded twice");
        Require(!ShouldIssueScrubPreview(10.1,10.0,true),"a movement inside one keyframe is folded away");
        Require(ShouldIssueScrubPreview(10.0+ScrubPreviewMinimumDeltaSeconds,10.0,true),"the threshold itself is issued");
        Require(ShouldIssueScrubPreview(9.0,10.0,true),"a backwards movement is issued");
        Require(!ShouldIssueScrubPreview(-1.0,10.0,true),"a negative position is never issued");
        Require(!ShouldIssueScrubPreview(
            std::numeric_limits<double>::quiet_NaN(),10.0,true),"a position with no value is never issued");
    }

    void TestPlaybackTimeFormatting()
    {
        using HaloDesktop::Playback::FormatPlaybackTime;

        Require(FormatPlaybackTime(0.0,false)==L"0:00","a fresh position reads as zero");
        Require(FormatPlaybackTime(65.4,false)==L"1:05","seconds are truncated, not rounded");
        Require(FormatPlaybackTime(3599.0,false)==L"59:59","minutes run past sixty when hours are off");
        Require(FormatPlaybackTime(3661.0,true)==L"1:01:01","hours are padded to two-digit minutes");
        Require(FormatPlaybackTime(59.0,true)==L"0:00:59","an hours-shaped file keeps its shape early on");
        Require(FormatPlaybackTime(-5.0,false)==L"0:00","a position before the start reads as zero");
    }

    void TestScopedReentrancyGuard()
    {
        bool active{};
        {
            HaloDesktop::Playback::ScopedReentrancyGuard const outer{active};
            Require(static_cast<bool>(outer)&&active,"the outer callback did not acquire its guard");
            HaloDesktop::Playback::ScopedReentrancyGuard const nested{active};
            Require(!static_cast<bool>(nested)&&active,"a nested callback bypassed its guard");
        }
        Require(!active,"the outer callback did not release its guard");

        try
        {
            HaloDesktop::Playback::ScopedReentrancyGuard const guard{active};
            throw std::runtime_error{"test unwind"};
        }
        catch(std::runtime_error const&)
        {
        }
        Require(!active,"exception unwinding left the callback guard active");
    }

    void RequireInvalid(std::function<void()> const& action,char const* message)
    {
        try
        {
            action();
        }
        catch(std::invalid_argument const&)
        {
            return;
        }
        throw std::runtime_error(message);
    }

    void RequireThrows(std::function<void()> const& action,char const* message)
    {
        try
        {
            action();
        }
        catch(std::exception const&)
        {
            return;
        }
        throw std::runtime_error(message);
    }

    void TestLanguages()
    {
        Require(HaloDesktop::Playback::NormalizeLanguage(L"EN-us")==L"eng","regional English was not normalized");
        Require(HaloDesktop::Playback::LanguageDisplayName(L"fre")==L"French","language display alias was not normalized");
        Require(HaloDesktop::Playback::LanguageMatches(L"ger",L"deu"),"bibliographic German did not match terminologic German");
        std::vector<HaloDesktop::Playback::TrackInfo> tracks{
            {1,HaloDesktop::Playback::TrackType::Subtitle,L"English",L"",L"ASS",false,false,L"eng"},
            {2,HaloDesktop::Playback::TrackType::Subtitle,L"Japanese addon",L"",L"SRT",false,true,L"jpn",L"addon:id"},
            {3,HaloDesktop::Playback::TrackType::Subtitle,L"Japanese",L"",L"PGS",true,false,L"jpn"},
        };
        Require(HaloDesktop::Playback::FindLanguageTrack(tracks,HaloDesktop::Playback::TrackType::Subtitle,L"jpn",true)==3,"embedded language match did not win");
        Require(HaloDesktop::Playback::TrackSummary(tracks,HaloDesktop::Playback::TrackType::Subtitle)==L"Japanese · PGS","selected subtitle summary was not truthful");
        tracks[2].Selected=false;
        Require(HaloDesktop::Playback::TrackSummary(tracks,HaloDesktop::Playback::TrackType::Subtitle)==L"Off","disabled subtitles were not reported as off");
        auto const encoded=HaloDesktop::Playback::EncodeExternalSubtitleTrackTitle(L"private-addon:id",L"Japanese · Provider");
        auto const decoded=HaloDesktop::Playback::DecodeExternalSubtitleTrackTitle(encoded);
        Require(decoded&&decoded->first==L"private-addon:id"&&decoded->second==L"Japanese · Provider","external subtitle identity did not stay separate from its display title");
        Require(!HaloDesktop::Playback::DecodeExternalSubtitleTrackTitle(L"Japanese · Provider"),"ordinary track title was treated as an encoded identity");
        Require(HaloDesktop::Playback::CanApplyAutomaticSelection(4,4,4),"initial automatic selection was rejected");
        Require(HaloDesktop::Playback::CanApplyAutomaticSelection(5,4,5),"latest automatic selection was rejected");
        Require(!HaloDesktop::Playback::CanApplyAutomaticSelection(6,4,5),"newer manual selection did not supersede automatic selection");
    }

    void TestResume()
    {
        using HaloDesktop::Playback::ShouldApplyResume;
        Require(ShouldApplyResume(true,false,180.0,1200.0,2.0,4,4,true),"eligible resume was rejected");
        Require(!ShouldApplyResume(true,true,180.0,1200.0,2.0,4,4,true),"watched media resumed");
        Require(!ShouldApplyResume(true,false,180.0,1200.0,6.0,4,4,true),"late playback position was rewound");
        Require(!ShouldApplyResume(true,false,180.0,1200.0,2.0,5,4,true),"a user seek was overwritten");
        Require(!ShouldApplyResume(true,false,1150.0,1200.0,2.0,4,4,true),"near-complete media resumed");
        Require(!ShouldApplyResume(true,false,180.0,1200.0,2.0,4,4,false),"expired startup resume was applied");
    }

    void TestHeaders()
    {
        auto const serialized=HaloDesktop::Playback::SerializePlaybackHeaders({{L"X-Test",L"a,b\\c"},{L"Authorization",L"Bearer test"}});
        Require(serialized==L"X-Test: a\\,b\\\\c,Authorization: Bearer test","header list escaping changed");
        RequireInvalid([]{HaloDesktop::Security::ValidateProtectedHttpHeaders(HaloDesktop::Security::ProtectedHttpHeaders{{L"Range",L"bytes=0-1"}});},"caller range header was accepted");
        RequireInvalid([]{HaloDesktop::Security::ValidateProtectedHttpHeaders(HaloDesktop::Security::ProtectedHttpHeaders{{L"Keep-Alive",L"timeout=30"}});},"hop-by-hop header was accepted");
        RequireInvalid([]{HaloDesktop::Security::ValidateProtectedHttpHeaders(HaloDesktop::Security::ProtectedHttpHeaders{{L"X-Test",L"safe\r\ninjected"}});},"header injection was accepted");
        RequireInvalid([]{HaloDesktop::Security::ValidateProtectedHttpHeaders(HaloDesktop::Security::ProtectedHttpHeaders{{L"Bad Header",L"value"}});},"invalid header name was accepted");
    }

    void TestPlaybackTransitions()
    {
        using namespace HaloDesktop::Playback;
        Require(!ResolveBufferingState(true,std::nullopt,true,false),"playback restart did not clear startup buffering");
        Require(ResolveBufferingState(false,true,true,true),"cache pause did not override playback-ready state");
        Require(ResolveBufferingState(true,std::nullopt,true,true),"playback restart overrode active cache buffering");
        Require(!ResolveBufferingState(true,false,false,false),"cache resume did not clear rebuffering");

        Require(IsPlaybackStalled(true,false,false),"a starved cache did not stall playback");
        Require(IsPlaybackStalled(false,true,false),"an unresolved seek did not stall playback");
        Require(!IsPlaybackStalled(true,true,true),"a paused player reported a stall");
        Require(!IsPlaybackStalled(false,false,false),"running playback reported a stall");
        Require(BufferingIndicatorDelay(false)==std::chrono::milliseconds::zero(),"opening a file delayed the indicator behind a black surface");
        Require(BufferingIndicatorDelay(true)>std::chrono::milliseconds::zero(),"a stall after the first frame skipped the anti-flicker delay");
        Require(BufferingIndicatorHoldRemaining(std::chrono::milliseconds::zero())>std::chrono::milliseconds::zero(),"an indicator shown this instant was allowed to hide at once");
        Require(BufferingIndicatorHoldRemaining(std::chrono::hours{1})==std::chrono::milliseconds::zero(),"a long-standing indicator was still held open");
        Require(BufferingIndicatorHoldRemaining(std::chrono::milliseconds{200})
                    ==BufferingIndicatorHoldRemaining(std::chrono::milliseconds::zero())-std::chrono::milliseconds{200},
                "the remaining hold did not shrink with the time already shown");
        Require(ShouldReportPlaybackChange(true,false,true,PlaybackEndReason::Eof),"combined EOF transition did not request a report");
        Require(ShouldReportPlaybackChange(false,false,true,PlaybackEndReason::None),"a user pause did not request a report");
        Require(!ShouldReportPlaybackChange(false,false,false,PlaybackEndReason::None),"cache starvation requested a watch report");
        Require(!ShouldReportPlaybackChange(false,true,true,PlaybackEndReason::Eof),"the terminal pause sent a duplicate watch report");
        Require(ShouldExitMpvEventLoop(true,false),"mpv event loop ignored a shutdown request while events remained queued");
        Require(ShouldExitMpvEventLoop(false,true),"mpv event loop ignored the shutdown event");
        Require(!ShouldExitMpvEventLoop(false,false),"mpv event loop stopped during normal playback");

        auto const ordinaryTimeline = NormalizePlaybackTimeline(90.0, 120.0);
        Require(ordinaryTimeline.PositionSeconds == 90.0
                && ordinaryTimeline.DurationSeconds == 120.0,
                "a valid playback timeline was changed");
        auto const pastEndTimeline = NormalizePlaybackTimeline(121.0, 120.0);
        Require(pastEndTimeline.PositionSeconds == 120.0
                && pastEndTimeline.DurationSeconds == 120.0,
                "a playback position beyond the duration was not clamped");
        auto const negativeTimeline = NormalizePlaybackTimeline(-1.0, 120.0);
        Require(negativeTimeline.PositionSeconds == 0.0,
                "a negative playback position was not clamped");
        auto const invalidPositionTimeline = NormalizePlaybackTimeline(
            (std::numeric_limits<double>::quiet_NaN)(),
            120.0);
        Require(invalidPositionTimeline.PositionSeconds == 0.0
                && invalidPositionTimeline.DurationSeconds == 120.0,
                "a non-finite playback position reached the timeline");
        auto const invalidDurationTimeline = NormalizePlaybackTimeline(
            90.0,
            (std::numeric_limits<double>::infinity)());
        Require(invalidDurationTimeline.PositionSeconds == 0.0
                && invalidDurationTimeline.DurationSeconds == 0.0,
                "a non-finite playback duration reached the timeline");

        Require(IsPlaybackSpeedSelected(1.75,1.75),"active speed was not selected");
        Require(!IsPlaybackSpeedSelected(1.75,1.5),"inactive speed was selected");
        Require(AdjustPlaybackDelayMilliseconds(0,50)==50,"delay did not advance by 50 ms");
        Require(AdjustPlaybackDelayMilliseconds(4990,50)==5000,"positive delay limit was not enforced");
        Require(AdjustPlaybackDelayMilliseconds(-4990,-50)==-5000,"negative delay limit was not enforced");
    }

    void TestSubtitleIntents()
    {
        using namespace HaloDesktop::Playback;
        TrackInfo normalized{7,TrackType::Subtitle,L"  English   SDH ",L"",L" SRT ",false,false,L"EN-us"};
        TrackInfo equivalent{8,TrackType::Subtitle,L"english sdh",L"",L"srt",false,false,L"eng"};
        TrackInfo different{9,TrackType::Subtitle,L"English",L"",L"ASS",false,false,L"eng"};
        auto const fingerprint=SubtitleTrackFingerprint(normalized);
        Require(fingerprint==SubtitleTrackFingerprint(equivalent),"subtitle fingerprint normalization was unstable");
        Require(fingerprint!=SubtitleTrackFingerprint(different),"different embedded tracks shared a fingerprint");
        Require(FindEmbeddedSubtitleByFingerprint({different,equivalent},fingerprint)==8,"embedded replay did not resolve the exact fingerprint");
        equivalent.External=true;
        Require(!FindEmbeddedSubtitleByFingerprint({equivalent},fingerprint),"external subtitle satisfied an embedded fingerprint");

        Require(ResolveSubtitleIntent(SubtitleIntentKind::Off,true)==SubtitleIntentResolution::Disable,"Off was not preserved for replay");
        Require(ResolveSubtitleIntent(SubtitleIntentKind::Off,false)==SubtitleIntentResolution::GlobalPreference,"Off incorrectly carried into the next episode");
        Require(ResolveSubtitleIntent(SubtitleIntentKind::Embedded,true)==SubtitleIntentResolution::ExactOnly,"embedded replay was not exact");
        Require(ResolveSubtitleIntent(SubtitleIntentKind::Embedded,false)==SubtitleIntentResolution::LanguageFallback,"embedded intent did not fall back by language on the next episode");
        Require(ResolveSubtitleIntent(SubtitleIntentKind::Addon,true)==SubtitleIntentResolution::ExactThenLanguage,"addon replay compatibility changed");
        Require(ResolveSubtitleIntent(SubtitleIntentKind::Addon,false)==SubtitleIntentResolution::LanguageFallback,"addon language memory did not carry into the next episode");
    }

    void TestProtectedHashPolicy()
    {
        using namespace HaloDesktop::Api;
        HaloDesktop::Security::ProtectedHttpHeaders protectedHeaders{
            {L"Authorization",L"Bearer protected"},
            {L"Referer",L"https://trusted.example/"},
        };
        auto const headHeaders=BuildHashRequestHeaders(protectedHeaders);
        auto const rangeHeaders=BuildHashRequestHeaders(protectedHeaders,HashByteRange{0,OpenSubtitlesHashChunkSize-1});
        auto const isolatedHeaders=BuildHashRequestHeaders({});
        Require(headHeaders==protectedHeaders,"required headers were not applied to HEAD");
        Require(rangeHeaders.size()==3&&rangeHeaders[0]==protectedHeaders[0]&&rangeHeaders[1]==protectedHeaders[1],"required headers were not isolated on the range request");
        Require(rangeHeaders[2].Name==L"Range"&&rangeHeaders[2].Value==L"bytes=0-65535","generated hash range was incorrect");
        Require(protectedHeaders.size()==2&&isolatedHeaders.empty(),"protected headers leaked between hash requests");
        RequireInvalid([]{static_cast<void>(BuildHashRequestHeaders({{L"Range",L"bytes=0-1"}}));},"unsafe supplied hash header was accepted");

        HashByteRange const requested{0,OpenSubtitlesHashChunkSize-1};
        HashContentRange const exact{0,OpenSubtitlesHashChunkSize-1,200'000};
        Require(ValidateHashRangeResponse(206,exact,requested,200'000,OpenSubtitlesHashChunkSize)==200'000,"exact hash range was rejected");
        RequireThrows([&]{static_cast<void>(ValidateHashRangeResponse(200,exact,requested,200'000,OpenSubtitlesHashChunkSize));},"non-206 hash response was accepted");
        RequireThrows([&]{static_cast<void>(ValidateHashRangeResponse(206,HashContentRange{1,OpenSubtitlesHashChunkSize,200'000},requested,200'000,OpenSubtitlesHashChunkSize));},"wrong hash range boundaries were accepted");
        RequireThrows([&]{static_cast<void>(ValidateHashRangeResponse(206,exact,requested,200'000,OpenSubtitlesHashChunkSize-1));},"short hash body was accepted");
        RequireThrows([&]{static_cast<void>(ValidateHashRangeResponse(206,exact,requested,210'000,OpenSubtitlesHashChunkSize));},"inconsistent head and tail totals were accepted");
        std::vector<std::uint8_t> zeros(OpenSubtitlesHashChunkSize);
        Require(ComputeOpenSubtitlesMovieHash(OpenSubtitlesHashChunkSize*2,zeros,zeros)==OpenSubtitlesHashChunkSize*2,"moviehash calculation changed");
    }

    void TestResponseSizePolicy()
    {
        using HaloDesktop::Api::CheckedResponseSize;
        using HaloDesktop::Api::MaximumJsonResponseBytes;
        using HaloDesktop::Api::ValidateDeclaredResponseSize;

        ValidateDeclaredResponseSize(MaximumJsonResponseBytes);
        RequireThrows([]{HaloDesktop::Api::ValidateDeclaredResponseSize(HaloDesktop::Api::MaximumJsonResponseBytes+1u);},"oversized declared JSON response was accepted");
        Require(CheckedResponseSize(MaximumJsonResponseBytes-1u,1u)==MaximumJsonResponseBytes,"exact JSON response limit was rejected");
        RequireThrows([]{static_cast<void>(HaloDesktop::Api::CheckedResponseSize(HaloDesktop::Api::MaximumJsonResponseBytes,1u));},"streamed JSON response exceeded its limit");
        RequireThrows([]{static_cast<void>(HaloDesktop::Api::CheckedResponseSize((std::numeric_limits<std::size_t>::max)(),1u));},"JSON response size overflow was accepted");
    }

    void TestWindowPresentationPolicy()
    {
        using HaloDesktop::Shell::CalculateFullscreenWindowPolicy;
        using HaloDesktop::Shell::FullscreenTransitionOutcome;
        using HaloDesktop::Shell::FullscreenZOrder;
        using HaloDesktop::Shell::ResolveFullscreenState;

        auto const unrelatedStyle = static_cast<LONG_PTR>(WS_VISIBLE | WS_CLIPCHILDREN);
        auto const unrelatedExtendedStyle = static_cast<LONG_PTR>(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
        auto const policy = CalculateFullscreenWindowPolicy(
            static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW) | unrelatedStyle,
            static_cast<LONG_PTR>(WS_EX_WINDOWEDGE | WS_EX_TOPMOST) | unrelatedExtendedStyle);

        Require((policy.Style & static_cast<LONG_PTR>(WS_POPUP)) != 0,
                "fullscreen policy did not apply the popup style");
        Require((policy.Style & unrelatedStyle) == unrelatedStyle,
                "fullscreen policy removed unrelated window style bits");
        Require((policy.ExtendedStyle & static_cast<LONG_PTR>(WS_EX_TOPMOST)) == 0,
                "fullscreen policy retained WS_EX_TOPMOST");
        Require((policy.ExtendedStyle & unrelatedExtendedStyle) == unrelatedExtendedStyle,
                "fullscreen policy removed unrelated extended style bits");
        Require(policy.ZOrder == FullscreenZOrder::ForegroundNonTopmost,
                "fullscreen policy selected a popup-obscuring z-order");

        Require(ResolveFullscreenState(false, true, FullscreenTransitionOutcome::Succeeded),
                "successful fullscreen entry did not commit fullscreen state");
        Require(!ResolveFullscreenState(true, false, FullscreenTransitionOutcome::Succeeded),
                "successful fullscreen exit did not commit windowed state");
        Require(!ResolveFullscreenState(false, true, FullscreenTransitionOutcome::Failed),
                "failed fullscreen entry changed presentation state");
        Require(ResolveFullscreenState(true, false, FullscreenTransitionOutcome::Failed),
                "failed fullscreen exit changed presentation state");
    }

    void TestPlayerOverlayMoveState()
    {
        using HaloDesktop::Shell::PlayerOverlayLifecycle;
        HaloDesktop::Shell::PlayerOverlayMoveState state;
        Require(!state.CanOpen(PlayerOverlayLifecycle::Unloaded), "an unloaded player could open its overlay");
        Require(state.CanOpen(PlayerOverlayLifecycle::Ready), "a loaded player could not open its overlay");

        Require(state.Enter(), "move-size entry was not recorded");
        Require(!state.Enter(), "duplicate move-size entry requested a second close");
        Require(state.IsActive(), "move-size state was not active after entry");
        Require(!state.CanOpen(PlayerOverlayLifecycle::Ready), "the overlay could reopen during move-size");

        Require(!state.Exit(PlayerOverlayLifecycle::Unloaded), "an unloaded player requested overlay restoration");
        Require(!state.IsActive(), "move-size state remained active after exit");

        Require(state.Enter(), "a later move-size entry was ignored");
        Require(!state.Exit(PlayerOverlayLifecycle::Closing), "a closing player requested overlay restoration");
        Require(!state.CanOpen(PlayerOverlayLifecycle::Closing), "teardown permitted a late overlay reopen");

        Require(state.Enter(), "the final move-size entry was ignored");
        Require(state.Exit(PlayerOverlayLifecycle::Ready), "a loaded player did not request restoration after exit");
        Require(state.CanOpen(PlayerOverlayLifecycle::Ready), "the overlay stayed blocked after move-size exit");

        Require(state.Enter(), "move-size entry before unload was ignored");
        state.Reset();
        Require(!state.IsActive(), "unload did not reset move-size state");
        Require(!state.CanOpen(PlayerOverlayLifecycle::Unloaded), "unload permitted a late overlay callback to reopen");
    }

    void TestVideoQualityBadges()
    {
        using HaloDesktop::Playback::ClassifyVideoQuality;
        using HaloDesktop::Playback::VideoDynamicRange;
        using HaloDesktop::Playback::VideoFormat;
        auto const badge=[](std::int32_t width,std::int32_t height,VideoDynamicRange range=VideoDynamicRange::Standard)
        {
            return ClassifyVideoQuality(VideoFormat{width,height,range});
        };

        Require(badge(3840,2160).Tier==L"4K","a 4K master was not badged as 4K");
        Require(badge(3840,2160).Detail==L"ULTRA HD","a 4K master lost its qualifier");
        Require(badge(4096,1716).Tier==L"4K","a DCI scope master was not badged as 4K");
        // The reason the tiers classify on area: scope framing costs a quarter of the
        // height, and portrait video hands the larger number to the wrong dimension.
        Require(badge(3840,1600).Tier==L"4K","a 2.39:1 4K master was demoted by its letterboxed height");
        Require(badge(2160,3840).Tier==L"4K","a portrait 4K clip was not badged as 4K");
        Require(badge(1080,1920).Tier==L"1080P","a portrait 1080p clip was promoted by its height");
        Require(badge(3440,1440).Tier==L"1440P","an ultrawide 1440p panel capture was promoted by its width");
        Require(badge(2560,1440).Tier==L"1440P","a 1440p master was not badged as QHD");
        Require(badge(2560,1440).Detail==L"QHD","a 1440p master lost its qualifier");
        Require(badge(2560,1080).Tier==L"1080P","an ultrawide 1080p master was promoted by its width");
        Require(badge(1920,1080).Tier==L"1080P","a 1080p master was not badged as full HD");
        Require(badge(1920,1080).Detail==L"FULL HD","a 1080p master lost its qualifier");
        Require(badge(1920,800).Tier==L"1080P","a 2.39:1 1080p master was demoted by its letterboxed height");
        Require(badge(1440,1080).Tier==L"1080P","an anamorphic 1080p master was demoted");
        Require(badge(1280,720).Tier==L"720P","a 720p master was not badged as HD");
        Require(badge(1280,536).Tier==L"720P","a 2.39:1 720p master was demoted by its letterboxed height");
        Require(badge(854,480).Tier==L"SD","a 480p file was not badged as SD");
        Require(badge(854,480).Detail.empty(),"an SD file gained a qualifier it does not need");
        Require(badge(720,576).Tier==L"SD","a PAL file was not badged as SD");

        Require(badge(3840,2160,VideoDynamicRange::Hdr).Detail==L"HDR","a PQ master did not report HDR");
        Require(badge(3840,2160,VideoDynamicRange::Hlg).Detail==L"HLG","an HLG master did not report HLG");
        Require(badge(3840,2160,VideoDynamicRange::DolbyVision).Detail==L"DOLBY VISION","a Dolby Vision master did not report it");
        Require(badge(3840,2160,VideoDynamicRange::Hdr).Tier==L"4K","dynamic range overwrote the resolution tier");
        Require(badge(1920,1080,VideoDynamicRange::DolbyVision).Detail==L"DOLBY VISION","Dolby Vision was reported only at 4K");

        Require(badge(0,0).Tier.empty(),"an undescribed video earned a badge");
        Require(badge(1920,0).Tier.empty(),"a video with no height earned a badge");
        Require(badge(-1920,-1080).Tier.empty(),"a negative frame size earned a badge");
    }

    void TestAddonSelection()
    {
        using HaloDesktop::Services::AddonIdentity;
        using HaloDesktop::Services::SelectDistinctAddons;
        auto const select=[](std::vector<AddonIdentity> const&addons){return SelectDistinctAddons(addons);};

        Require(select({}).empty(),"an empty addon list produced selections");

        std::vector<AddonIdentity> const distinct{{L"com.linvo.cinemeta",true},{L"org.stremio.opensubtitlesv3",true}};
        Require(select(distinct)==std::vector<std::size_t>{0,1},"unrelated addons were collapsed");

        // The shape that duplicated every Cinemeta shelf on Home: the same addon
        // installed globally and by the user.
        std::vector<AddonIdentity> const both{{L"com.linvo.cinemeta",true},{L"org.stremio.opensubtitlesv3",true},{L"com.linvo.cinemeta",false}};
        Require(select(both)==std::vector<std::size_t>{2,1},"an addon in both lists was selected twice");

        std::vector<AddonIdentity> const userFirst{{L"com.linvo.cinemeta",false},{L"com.linvo.cinemeta",true}};
        Require(select(userFirst)==std::vector<std::size_t>{0},"a global row displaced the user's own row");

        std::vector<AddonIdentity> const twoUserRows{{L"com.example.mirror",false},{L"com.example.mirror",false}};
        Require(select(twoUserRows)==std::vector<std::size_t>{0},"two of the user's own rows for one addon were kept");

        std::vector<AddonIdentity> const anonymous{{L"",true},{L"",false},{L"com.linvo.cinemeta",true}};
        Require(select(anonymous)==std::vector<std::size_t>{0,1,2},"addons with no manifest id were merged together");
    }

    void TestTemporaryFileCleanup()
    {
        auto const suffix=std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
        auto const root=std::filesystem::temp_directory_path()/(L"halo-playback-tests-"+suffix);
        Require(std::filesystem::create_directory(root),"temporary test directory could not be created");
        struct DirectoryCleanup final
        {
            std::filesystem::path Path;
            ~DirectoryCleanup(){std::error_code error;std::filesystem::remove_all(Path,error);}
        };
        [[maybe_unused]] DirectoryCleanup cleanup{root};
        auto const first=root/L"first.srt";auto const second=root/L"second.ass";
        std::ofstream{first}<<"first";std::ofstream{second}<<"second";
        Require(std::filesystem::is_regular_file(first)&&std::filesystem::is_regular_file(second),"temporary subtitle fixtures were not created");
        HaloDesktop::Playback::TemporaryFileCollection files;files.Add(first);files.Add(second);
        Require(files.Size()==2,"temporary subtitle ownership was not recorded");
        files.Cleanup();
        Require(files.Size()==0,"temporary subtitle ownership remained after cleanup");
        Require(!std::filesystem::exists(first)&&!std::filesystem::exists(second),"temporary subtitle files remained after cleanup");
    }

    void TestReleaseFileMatching()
    {
        using HaloDesktop::Services::ReleaseLeafName;
        using HaloDesktop::Services::SameReleaseFile;

        // Addons disagree about casing on the same release, so a case difference
        // must not make a saved file look like a different one.
        Require(SameReleaseFile(L"Show.S01E02.1080p.mkv",L"Show.S01E02.1080p.mkv"),"an identical name is the same file");
        Require(SameReleaseFile(L"Show.S01E02.1080p.mkv",L"show.s01e02.1080p.MKV"),"casing alone is not a different file");
        Require(!SameReleaseFile(L"Show.S01E02.1080p.mkv",L"Show.S01E03.1080p.mkv"),"a different episode is a different file");
        Require(!SameReleaseFile(L"Show.S01E02.1080p.mkv",L"Show.S01E02.1080p.mkv.part"),"a longer name is a different file");

        // Some addons name the file inside its release folder. The leaf is what
        // both sides agree on, and either separator can appear.
        Require(SameReleaseFile(L"Show.S01E02.1080p.mkv",L"Show.S01E02.1080p/Show.S01E02.1080p.mkv"),"a folder prefix is not part of the name");
        Require(SameReleaseFile(L"Show.S01E02.1080p.mkv",LR"(Release\Show.S01E02.1080p.mkv)"),"a backslash prefix is not part of the name");
        Require(SameReleaseFile(L"  Show.S01E02.1080p.mkv ",L"Show.S01E02.1080p.mkv"),"surrounding whitespace is not part of the name");

        // An addon that named nothing must not match the one download that also
        // named nothing, or a single saved file would claim every unnamed source.
        Require(!SameReleaseFile(L"",L""),"two unnamed sources are not the same file");
        Require(!SameReleaseFile(L"Show.S01E02.1080p.mkv",L""),"an unnamed source matches nothing");
        Require(!SameReleaseFile(L"   ",L"Show.S01E02.1080p.mkv"),"a whitespace name matches nothing");
        Require(!SameReleaseFile(L"Show.S01E02.1080p.mkv",L"Release/"),"a name that is only a folder matches nothing");

        Require(ReleaseLeafName(L"a/b/c.mkv")==L"c.mkv","the leaf is the part after the last separator");
        Require(ReleaseLeafName(L" c.mkv ")==L"c.mkv","the leaf is trimmed");
        Require(ReleaseLeafName(L"").empty(),"an empty name has no leaf");
    }
}

int main()
{
    try
    {
        TestScopedReentrancyGuard();
        TestLanguages();
        TestResume();
        TestHeaders();
        TestPlaybackTransitions();
        TestSubtitleIntents();
        TestProtectedHashPolicy();
        TestResponseSizePolicy();
        TestWindowPresentationPolicy();
        TestPlayerOverlayMoveState();
        TestVideoQualityBadges();
        TestAddonSelection();
        TestTemporaryFileCleanup();
        TestScrubPreviewMapping();
        TestScrubPreviewPlacement();
        TestScrubPreviewCoalescing();
        TestPlaybackTimeFormatting();
        TestReleaseFileMatching();
        std::cout<<"Playback policy tests passed.\n";
        return 0;
    }
    catch(std::exception const&error)
    {
        std::cerr<<"Playback policy tests failed: "<<error.what()<<'\n';
        return 1;
    }
}
