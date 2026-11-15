#include "Api/ActionEntryPolicy.h"
#include "Controls/ArtworkUrlPolicy.h"
#include "Controls/HaloMarkGeometry.h"
#include "Services/AccountLabelPolicy.h"
#include "Services/AddonListPolicy.h"
#include "Services/LanguagePolicy.h"
#include "Services/RatingPolicy.h"
#include "ViewModels/QualityMedalPolicy.h"
#include "Api/OpenSubtitlesHashPolicy.h"
#include "Api/ResponseSizePolicy.h"
#include "Playback/PlaybackPolicy.h"
#include "Playback/ScrubPreviewPolicy.h"
#include "Playback/ScopedReentrancyGuard.h"
#include "Playback/TemporaryFileCollection.h"
#include "Security/ProtectedHttpHeaders.h"
#include "Services/AddonSelectionPolicy.h"
#include "Services/DownloadSourceMatch.h"
#include "Services/Downloads/TransferRateWindow.h"
#include "Shell/ShellChromePolicy.h"
#include "Shell/WindowPresentationPolicy.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

    void TestScrubPreviewLoading()
    {
        using HaloDesktop::Playback::IsScrubPreviewLoading;
        using HaloDesktop::Playback::ScrubPreviewSkeletonDelay;

        Require(!IsScrubPreviewLoading(0,0),"a card that asked for nothing is not loading");
        Require(IsScrubPreviewLoading(1,0),"an unanswered first request is loading");
        Require(!IsScrubPreviewLoading(3,3),"an answered request, frame or empty, is not loading");
        Require(IsScrubPreviewLoading(4,3),"a newer request than the last answer is loading");
        Require(ScrubPreviewSkeletonDelay.count()>0,"the skeleton waits before it shows");
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
        using HaloDesktop::Shell::ResolveFullscreenTopmost;
        using HaloDesktop::Shell::WindowActivation;

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
        // The shell's own fullscreen detection runs once, at the SetWindowPos
        // that sizes the window, and is skipped when the window is not the
        // foreground window at that instant. The z-order must not depend on it.
        Require(policy.ZOrder == FullscreenZOrder::TopmostWhileActive,
                "fullscreen policy relied on the shell's fullscreen detection");
        Require(ResolveFullscreenTopmost(policy.ZOrder, WindowActivation::Active),
                "an active fullscreen window was not placed above the taskbar");
        Require(!ResolveFullscreenTopmost(policy.ZOrder, WindowActivation::Inactive),
                "an inactive fullscreen window stayed above other applications");

        Require(ResolveFullscreenState(false, true, FullscreenTransitionOutcome::Succeeded),
                "successful fullscreen entry did not commit fullscreen state");
        Require(!ResolveFullscreenState(true, false, FullscreenTransitionOutcome::Succeeded),
                "successful fullscreen exit did not commit windowed state");
        Require(!ResolveFullscreenState(false, true, FullscreenTransitionOutcome::Failed),
                "failed fullscreen entry changed presentation state");
        Require(ResolveFullscreenState(true, false, FullscreenTransitionOutcome::Failed),
                "failed fullscreen exit changed presentation state");
    }

    // Mirrors the reference's "medals are gold, silver and bronze by tier, tagged by
    // range, and tagless below HD" (sources.test.ts).
    void TestQualityMedals()
    {
        using HaloDesktop::ViewModels::Medal;
        using HaloDesktop::ViewModels::MedalFinish;
        using HaloDesktop::ViewModels::MedalFor;
        auto const medal=[](std::wstring_view quality,std::wstring_view range=L"SDR")
        {
            return MedalFor(quality,range);
        };

        Require(medal(L"2160p",L"DV")==Medal{L"4K",L"DV",MedalFinish::Gold},"a Dolby Vision 4K source was not a gold DV medal");
        Require(medal(L"2160p")==Medal{L"4K",L"UHD",MedalFinish::Gold},"a 4K source was not a gold UHD medal");
        Require(medal(L"1080p",L"HDR10")==Medal{L"1080p",L"HDR",MedalFinish::Silver},"an HDR10 1080p source was not a silver HDR medal");
        Require(medal(L"1440p")==Medal{L"1440p",L"QHD",MedalFinish::Silver},"a 1440p source was not a silver QHD medal");
        Require(medal(L"720p")==Medal{L"720p",L"HD",MedalFinish::Bronze},"a 720p source was not a bronze HD medal");
        Require(medal(L"480p")==Medal{L"480p",L"",MedalFinish::Plain},"a 480p source gained a tag or a metal");
        // The reference prints an em dash; this repository uses an en dash for it.
        Require(medal(L"UNKNOWN")==Medal{L"\x2013",L"",MedalFinish::Plain},"an unparsed quality was not a plain dash medal");

        // Every range that is present and not SDR reads as HDR; an absent range is SDR.
        Require(medal(L"2160p",L"HLG").Tag==L"HDR","an HLG source was not tagged HDR");
        Require(medal(L"720p",L"HDR").Tag==L"HDR","an HDR source was not tagged HDR");
        Require(medal(L"1080p",L"").Tag==L"FHD","a source without a range lost its tier tag");
        Require(medal(L"SD",L"DV")==Medal{L"SD",L"DV",MedalFinish::Plain},"a Dolby Vision SD source lost its tag or gained a metal");
    }

    // Mirrors the reference's "quality is classified by pixels, and dynamic range
    // replaces the qualifier" (playerLogic.test.ts).
    void TestQualityMedalsFromPicture()
    {
        using HaloDesktop::Playback::VideoDynamicRange;
        using HaloDesktop::Playback::VideoFormat;
        using HaloDesktop::ViewModels::Medal;
        using HaloDesktop::ViewModels::MedalFinish;
        using HaloDesktop::ViewModels::MedalForPicture;
        auto const video=[](std::int32_t width,std::int32_t height,VideoDynamicRange range=VideoDynamicRange::Standard)
        {
            return MedalForPicture(VideoFormat{width,height,range});
        };

        Require(video(3840,1600)==Medal{L"4K",L"UHD",MedalFinish::Gold},"a scope 4K master was not a gold UHD medal");
        Require(video(1920,800)==Medal{L"1080p",L"FHD",MedalFinish::Silver},"a scope 1080p master was not a silver FHD medal");
        Require(video(1280,720,VideoDynamicRange::Hdr)==Medal{L"720p",L"HDR",MedalFinish::Bronze},"a PQ 720p master was not a bronze HDR medal");
        Require(video(3840,2160,VideoDynamicRange::DolbyVision)==Medal{L"4K",L"DV",MedalFinish::Gold},"a Dolby Vision 4K master was not a gold DV medal");
        Require(video(720,480,VideoDynamicRange::Hlg)==Medal{L"SD",L"HLG",MedalFinish::Plain},"an HLG SD file was not a plain HLG medal");
        Require(video(720,480)==Medal{L"SD",L"",MedalFinish::Plain},"an SD file gained a tag or a metal");
        Require(!video(0,0).has_value(),"a file with no picture was given a medal");
        Require(video(2560,1440)==Medal{L"1440p",L"QHD",MedalFinish::Silver},"a 1440p master was not a silver QHD medal");
    }

    // The mark's bars, cut by the front ring's clearance band, as HaloMark.tsx masks
    // them: nothing of a bar is left inside the band, and the rest of each bar is.
    void TestHaloMarkGeometry()
    {
        using HaloDesktop::Controls::HaloMarkBarPieces;
        using HaloDesktop::Controls::HaloMarkFrontRingPoint;
        using HaloDesktop::Controls::HaloMarkRingClearance;
        using HaloDesktop::Controls::HaloMarkRingWidth;
        using HaloDesktop::Controls::HaloMarkWeight;
        using HaloDesktop::Controls::HaloMarkWeightFor;
        using HaloDesktop::Controls::MarkPoint;
        using HaloDesktop::Controls::MarkPolygon;

        Require(HaloMarkWeightFor(16.0)==HaloMarkWeight::Small,"a 16px mark was not drawn heavier");
        Require(HaloMarkWeightFor(24.0)==HaloMarkWeight::Small,"a 24px mark was not drawn heavier");
        Require(HaloMarkWeightFor(25.0)==HaloMarkWeight::Regular,"a 25px mark was drawn heavier");
        Require(HaloMarkRingWidth(HaloMarkWeight::Small)==7.0&&HaloMarkRingClearance(HaloMarkWeight::Small)==3.0,
                "a small mark's ring is not 7 wide with 3 clearance");
        Require(HaloMarkRingWidth(HaloMarkWeight::Regular)==5.0&&HaloMarkRingClearance(HaloMarkWeight::Regular)==5.0,
                "a regular mark's ring is not 5 wide with 5 clearance");

        auto const left=HaloMarkFrontRingPoint(0.0);
        auto const right=HaloMarkFrontRingPoint(1.0);
        // rotate(-16 32 34) applied to (5,34) and (59,34).
        Require(std::abs(left.X-6.0459)<0.001&&std::abs(left.Y-41.4422)<0.001,"the front ring does not start at the tilted left end");
        Require(std::abs(right.X-57.9541)<0.001&&std::abs(right.Y-26.5578)<0.001,"the front ring does not end at the tilted right end");

        auto const distanceToRing=[](MarkPoint point)
        {
            auto best=(std::numeric_limits<double>::max)();
            for(int index=0;index<=20000;++index)
            {
                auto const sample=HaloMarkFrontRingPoint(static_cast<double>(index)/20000.0);
                best=(std::min)(best,std::hypot(point.X-sample.X,point.Y-sample.Y));
            }
            return best;
        };
        // Even-odd point-in-polygon.
        auto const inside=[](MarkPolygon const& polygon,MarkPoint point)
        {
            bool result=false;
            for(std::size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++)
            {
                auto const& a=polygon[i];
                auto const& b=polygon[j];
                if((a.Y>point.Y)!=(b.Y>point.Y)
                    &&point.X<(b.X-a.X)*(point.Y-a.Y)/(b.Y-a.Y)+a.X)
                {
                    result=!result;
                }
            }
            return result;
        };

        for(auto const weight:{HaloMarkWeight::Regular,HaloMarkWeight::Small})
        {
            auto const half=(HaloMarkRingWidth(weight)+HaloMarkRingClearance(weight))/2.0;
            auto const pieces=HaloMarkBarPieces(weight);
            Require(pieces.size()==4,"the mark did not split each bar into a top and a bottom piece");
            for(auto const& piece:pieces)
            {
                Require(piece.size()>=3,"a bar piece is not a polygon");
                for(auto const& point:piece)
                {
                    auto const inLeftBar=point.X>=13.0-1e-9&&point.X<=25.0+1e-9;
                    auto const inRightBar=point.X>=39.0-1e-9&&point.X<=51.0+1e-9;
                    Require(inLeftBar||inRightBar,"a bar piece reaches outside its bar");
                    Require(point.Y>=7.0-1e-9&&point.Y<=57.0+1e-9,"a bar piece reaches above or below its bar");
                }
            }
            // A grid over both bars: a point inside a bar's rounded outline is drawn
            // exactly when it stands clear of the band.
            for(double barLeft:{13.0,39.0})
            {
                for(double x=barLeft+0.25;x<barLeft+12.0;x+=0.5)
                {
                    for(double y=7.25;y<57.0;y+=0.5)
                    {
                        auto const cx=barLeft+6.0;
                        auto const cy=std::clamp(y,13.0,51.0);
                        if(std::hypot(x-cx,y-cy)>6.0-0.05)continue;
                        auto const distance=distanceToRing({x,y});
                        if(std::abs(distance-half)<0.05)continue;
                        auto drawn=false;
                        for(auto const& piece:pieces)drawn=drawn||inside(piece,{x,y});
                        Require(drawn==(distance>half),"a bar point was drawn inside the band, or cut outside it");
                    }
                }
            }
        }
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

    void TestTransferRateWindow()
    {
        using HaloDesktop::Services::Downloads::TransferRateWindow;
        using namespace std::chrono_literals;
        using Clock = TransferRateWindow::Clock;

        auto const origin = Clock::time_point{};
        auto const tick = 250ms;
        constexpr std::uint64_t megabyte = 1'000'000;

        TransferRateWindow window{ 3s };
        window.Reset(origin, 0);
        std::uint64_t reported{};
        std::uint64_t bytes{};
        for (int step = 1; step <= 40; ++step)
        {
            bytes += megabyte;
            reported = window.Record(origin + tick * step, bytes);
        }
        Require(reported >= 3'900'000 && reported <= 4'100'000,
                "a steady 4 MB/s transfer was not reported as 4 MB/s");

        // A stall followed by a catch-up burst must not swing the average.
        reported = window.Record(origin + tick * 41, bytes);
        Require(reported >= 3'500'000 && reported <= 4'100'000,
                "one empty slice collapsed the windowed rate");
        bytes += 2 * megabyte;
        reported = window.Record(origin + tick * 42, bytes);
        Require(reported >= 3'500'000 && reported <= 4'500'000,
                "one catch-up slice inflated the windowed rate");

        // A restart from a smaller offset forgets the history.
        Require(window.Record(origin + tick * 43, megabyte) == 0,
                "a rewound byte count did not reset the window");
    }

    // Mirrors packages/core/test/actionEntries.test.ts. The reference's last
    // case (streams filtered as they are read) is the mapper's, in StabilityTests.
    void TestActionEntries()
    {
        using HaloDesktop::Api::IsActionEntry;
        using HaloDesktop::Api::IsActionStream;

        // MediaFusion's delete-all entry is not a title.
        Require(IsActionEntry(L"dltorbox", L"\U0001F5D1️\U0001F4A9 Delete all files"), "the delete-all entry was kept");
        Require(IsActionEntry(L"dlrealdebrid", L"Anything"), "an action id was kept");

        // An action named any other way is still caught.
        for (auto const name : { L"\U0001F5D1 Empty", L"Clear all watched", L"Remove all torrents", L"Wipe the library", L"Purge cache" })
        {
            Require(IsActionEntry(L"x:1", name), "an action named another way was kept");
        }

        // Real titles stay, whatever their id or name.
        std::vector<std::pair<std::wstring_view, std::wstring_view>> const titles{
            { L"tt12735488", L"Kalki 2898-AD" },
            { L"kitsu:1234", L"Delete" },
            { L"dl:42", L"Download Days" },
            { L"tt1", L"All Clear" },
            { L"tt2", L"Deleted Scenes" },
            { L"tt3", L"The Purge: Election Year" },
            { L"tt4", L"Delete History" },
        };
        for (auto const& [id, name] : titles)
        {
            Require(!IsActionEntry(id, name), "a real title was taken for an action");
        }

        // A stream that would delete is never offered, however it is labelled.
        Require(IsActionStream({ .Url = L"https://mf.test/streaming_provider/s/delete_all_watchlist?provider=torbox", .Name = L"TorBox" }),
            "a delete link was offered as a stream");
        Require(IsActionStream({ .Url = L"https://cdn.test/v.mkv", .Description = L"\U0001F6A8\U0001F480⚠ Delete all files in TorBox watchlist." }),
            "a stream described as a deletion was offered");
        Require(!IsActionStream({ .Url = L"https://cdn.test/Delete.History.2024.1080p.mkv", .Name = L"Delete History" }),
            "a film named Delete History was dropped");
        Require(!IsActionStream({ .Url = L"https://cdn.test/Deleted.Scenes.mkv", .Title = L"Deleted Scenes" }),
            "a film named Deleted Scenes was dropped");

        // The JavaScript pattern semantics the port keeps.
        Require(HaloDesktop::Api::MatchesActionText(L"DELETE ALL"), "the text pattern was case sensitive");
        Require(!HaloDesktop::Api::MatchesActionText(L"Delete\nall files"), "the gap crossed a line break");
        // The gap is everything between the verb and its target, spaces included.
        Require(HaloDesktop::Api::MatchesActionText(L"Delete " + std::wstring(38, L'x') + L" all"),
            "a forty-character gap was not crossed");
        Require(!HaloDesktop::Api::MatchesActionText(L"Delete " + std::wstring(39, L'x') + L" all"),
            "a gap longer than forty characters was crossed");
        Require(!HaloDesktop::Api::MatchesActionId(L"dl"), "a bare dl prefix was taken for an action id");
        Require(!HaloDesktop::Api::MatchesActionId(L"dlTorbox"), "the action id pattern was case insensitive");
        Require(HaloDesktop::Api::MatchesActionUrl(L"https://x.test/PURGE-EVERYTHING"), "the URL pattern was case sensitive");
    }

    // Mirrors packages/core/test/languages.test.ts, plus the tables around it.
    void TestLanguagePolicy()
    {
        using HaloDesktop::Services::LanguageCodeVariants;
        using HaloDesktop::Services::LanguageTagsMatch;

        // A track tagged with a two-letter or regional code matches the three-letter preference.
        Require(LanguageTagsMatch(L"en", L"eng"), "en did not match eng");
        Require(LanguageTagsMatch(L"en-US", L"eng"), "en-US did not match eng");
        Require(LanguageTagsMatch(L"EN_gb", L"eng"), "EN_gb did not match eng");
        Require(LanguageTagsMatch(L"de", L"ger"), "de did not match ger");
        Require(LanguageTagsMatch(L"deu", L"ger"), "deu did not match ger");
        Require(LanguageTagsMatch(L"English", L"eng"), "a label did not match its code");

        // Different languages do not match, including ones that share letters.
        Require(!LanguageTagsMatch(L"por", L"eng"), "por matched eng");
        Require(!LanguageTagsMatch(L"es", L"eng"), "es matched eng");
        Require(!LanguageTagsMatch(L"und", L"eng"), "und matched eng");
        Require(!LanguageTagsMatch(L"enm", L"eng"), "enm matched eng");

        // Brazilian Portuguese stays apart from Portuguese.
        Require(LanguageTagsMatch(L"pt-BR", L"pob"), "pt-BR did not match pob");
        Require(LanguageTagsMatch(L"pt", L"por"), "pt did not match por");
        Require(!LanguageTagsMatch(L"pt-BR", L"por"), "pt-BR matched por");
        Require(!LanguageTagsMatch(L"pt", L"pob"), "pt matched pob");

        // A preference lists every spelling of its language, its own first.
        Require(LanguageCodeVariants(L"eng") == std::vector<std::wstring>{ L"eng", L"en" }, "eng variants were wrong");
        Require(LanguageCodeVariants(L"ger") == std::vector<std::wstring>{ L"ger", L"deu", L"de" }, "ger variants were wrong");
        Require(LanguageCodeVariants(L"pob") == std::vector<std::wstring>{ L"pob", L"pt-br" }, "pob variants were wrong");
        Require(LanguageCodeVariants(L"").empty(), "an empty preference had variants");
        Require(LanguageCodeVariants(L"nor") == std::vector<std::wstring>{ L"nor", L"no", L"nb" }, "nor variants were wrong");

        // Labels: exact codes only for languageLabel; any spelling for the name of a tag.
        Require(HaloDesktop::Services::LanguageLabel(L" KOR ") == L"Korean", "a padded code did not find its label");
        Require(HaloDesktop::Services::LanguageLabel(L"ko") == L"ko", "languageLabel resolved a two-letter code");
        Require(HaloDesktop::Services::LanguageNameForTag(L"ko-KR").value_or(L"") == L"Korean", "a regional tag had no name");
        Require(HaloDesktop::Services::LanguageNameForTag(L"pt_BR").value_or(L"") == L"Portuguese (BR)", "pt_BR was not Brazilian Portuguese");
        Require(!HaloDesktop::Services::LanguageNameForTag(L"und"), "an unknown tag was given a name");
        auto const options = HaloDesktop::Services::LanguageOptions();
        Require(options.size() == 39 && options.front().Code == L"eng" && options.back().Code == L"lit",
            "the language options were not the reference list");

        // The player's helpers now read the full tables.
        Require(HaloDesktop::Playback::LanguageDisplayName(L"ko") == L"Korean", "the player did not name a two-letter Korean track");
        Require(HaloDesktop::Playback::LanguageDisplayName(L"xx") == L"xx", "an unknown tag was not shown as given");
        Require(HaloDesktop::Playback::LanguageMatches(L"sv", L"swe"), "the player did not match sv to swe");
        Require(!HaloDesktop::Playback::LanguageMatches(L"", L""), "two empty tags matched in the player");
    }

    // Mirrors the rating case of apps/desktop/tests/format.test.ts.
    void TestRatings()
    {
        using HaloDesktop::Services::RatedValue;
        using HaloDesktop::Services::RatingLabel;
        Require(RatingLabel(std::wstring_view{ L"6.8" }).value_or(L"") == L"\x2605 6.8", "a rating was not labelled");
        Require(!RatingLabel(std::wstring_view{ L"0.0" }), "0.0 was labelled as a rating");
        Require(!RatingLabel(std::wstring_view{ L"0" }), "0 was labelled as a rating");
        Require(!RatingLabel(std::wstring_view{ L"" }), "an empty rating was labelled");
        Require(!RatingLabel(std::nullopt), "a missing rating was labelled");
        Require(!RatingLabel(std::wstring_view{ L"N/A" }), "N/A was labelled as a rating");
        Require(RatedValue(std::wstring_view{ L" 7.2 " }).value_or(L"") == L"7.2", "a padded rating was not trimmed");
        // parseFloat reads a leading number and stops at the rest.
        Require(RatedValue(std::wstring_view{ L"7.2/10" }).value_or(L"") == L"7.2/10", "a rating with a scale was dropped");
        Require(RatedValue(std::wstring_view{ L".5" }).value_or(L"") == L".5", "a bare fraction was dropped");
        Require(!RatedValue(std::wstring_view{ L"-1" }), "a negative rating was kept");
        Require(!RatedValue(std::wstring_view{ L"1e999" }), "an infinite rating was kept");
    }

    // Mirrors apps/desktop/tests/artworkUrl.test.ts, plus the control's source order.
    void TestArtworkUrls()
    {
        using HaloDesktop::Controls::CardArtworkUrl;
        using HaloDesktop::Controls::MetahubPosterUrl;

        // A TMDB original image is asked for at card size.
        Require(CardArtworkUrl(L"https://image.tmdb.org/t/p/original/n8Df1H1TmcmZfjGgWh5uQyRFzHy.jpg")
                == L"https://image.tmdb.org/t/p/w500/n8Df1H1TmcmZfjGgWh5uQyRFzHy.jpg",
            "a TMDB original was not asked for at card size");
        Require(CardArtworkUrl(L"https://image.tmdb.org/t/p/original/a.jpg", 342) == L"https://image.tmdb.org/t/p/w342/a.jpg",
            "a TMDB original was not asked for at the given width");

        // Other images and TMDB sizes already chosen are left alone.
        for (auto const url : {
                 L"https://image.tmdb.org/t/p/w342/a.jpg",
                 L"https://images.metahub.space/poster/medium/tt1/img",
                 L"https://m.media-amazon.com/images/M/x._V1_UY396_.jpg",
                 L"http://image.tmdb.org/t/p/original/a.jpg",
                 L"https://example.com/image.tmdb.org/t/p/original/a.jpg",
             })
        {
            Require(CardArtworkUrl(url) == url, "an image that needed no rewrite was changed");
        }

        // An IMDb title falls back on its metahub poster; other ids have none.
        Require(MetahubPosterUrl(L"tt12735488").value_or(L"") == L"https://images.metahub.space/poster/medium/tt12735488/img",
            "an IMDb id had no metahub poster");
        for (auto const id : { L"", L"tt", L"tmdb:1234", L"mf:42", L"tt1:1:2", L"xtt1" })
        {
            Require(!MetahubPosterUrl(id), "an id that is not an IMDb id had a metahub poster");
        }

        // The control's order: the artwork, the caller's fallback, metahub, each once.
        auto const ordered = HaloDesktop::Controls::ArtworkCandidates(L"https://a.test/p.jpg", L"https://a.test/b.jpg", L"tt1");
        Require(ordered == std::vector<std::wstring>{
                    L"https://a.test/p.jpg", L"https://a.test/b.jpg", L"https://images.metahub.space/poster/medium/tt1/img" },
            "the artwork sources were not in order");
        Require(HaloDesktop::Controls::ArtworkCandidates(L"", L"", L"tt1")
                == std::vector<std::wstring>{ L"https://images.metahub.space/poster/medium/tt1/img" },
            "a missing poster did not fall through to metahub");
        Require(HaloDesktop::Controls::ArtworkCandidates(L"https://a.test/p.jpg", L"https://a.test/p.jpg", L"kitsu:1").size() == 1,
            "a repeated source was tried twice");
        Require(HaloDesktop::Controls::UsesCardArtwork(148.0) && HaloDesktop::Controls::UsesCardArtwork(500.0),
            "a card-sized slot did not ask for the card copy");
        Require(!HaloDesktop::Controls::UsesCardArtwork(0.0) && !HaloDesktop::Controls::UsesCardArtwork(544.0),
            "a backdrop or a slot wider than the card copy asked for it");
    }

    // Mirrors apps/desktop/tests/accountLabel.test.ts.
    void TestAccountLabel()
    {
        using HaloDesktop::Services::AccountLabel;
        using HaloDesktop::Services::AccountLabelInput;

        // A server's account tier replaces the account caption.
        Require(AccountLabel(AccountLabelInput{ .IsAdmin = true, .Plan = L"premium" }) == L"PREMIUM", "the tier did not replace the caption");
        Require(AccountLabel(AccountLabelInput{ .IsAdmin = false, .Plan = L"free" }) == L"FREE", "the free tier was not shown");

        // Without a tier the caption names the account and its admin role.
        Require(AccountLabel(AccountLabelInput{ .IsAdmin = true }) == L"ADMIN \x00B7 HALO ACCOUNT", "the admin caption was wrong");
        Require(AccountLabel(AccountLabelInput{ .IsAdmin = false }) == L"HALO ACCOUNT", "the account caption was wrong");
        Require(AccountLabel(std::nullopt) == L"HALO ACCOUNT", "no account did not read as a Halo account");

        Require(HaloDesktop::Services::AccountDisplayName(L"kenneth") == L"Kenneth", "the username was not capitalised");
        Require(HaloDesktop::Services::AccountDisplayName(L"élodie") == L"Élodie", "a non-ASCII first letter was not raised");
        Require(HaloDesktop::Services::AccountDisplayName(L"").empty(), "an empty username gained text");
        Require(HaloDesktop::Services::DeviceAccountName == L"This PC" && HaloDesktop::Services::DeviceAccountLabel == L"NOT SIGNED IN",
            "the device account copy was wrong");
    }

    void TestAddonListPolicy()
    {
        using HaloDesktop::Services::AddonEditInput;
        using HaloDesktop::Services::CanEditAddonInScope;
        using HaloDesktop::Services::EvaluateAddonEditability;

        Require(HaloDesktop::Services::DefaultAddonUrls.size() == 3
                && HaloDesktop::Services::IsDefaultAddonUrl(L"https://tmdb.elfhosted.com/manifest.json")
                && !HaloDesktop::Services::IsDefaultAddonUrl(L"https://tmdb.elfhosted.com/x/manifest.json"),
            "the built-in addons were not the reference three");

        // A non-admin never receives global transport URLs and keeps their own list.
        std::vector<AddonEditInput> const asUser{ { true, false }, { false, true }, { false, true } };
        auto const user = EvaluateAddonEditability(asUser, false);
        Require(user.User && !user.Global, "hidden global URLs locked a user out of their own list");
        Require(CanEditAddonInScope(user, false) && !CanEditAddonInScope(user, true), "a row was judged by the wrong scope");

        // An admin edits both lists when every URL is present.
        std::vector<AddonEditInput> const asAdmin{ { true, true }, { false, true } };
        auto const admin = EvaluateAddonEditability(asAdmin, true);
        Require(admin.User && admin.Global, "an admin could not edit complete lists");

        // A missing URL blocks only its own scope.
        std::vector<AddonEditInput> const brokenUser{ { true, true }, { false, false } };
        auto const broken = EvaluateAddonEditability(brokenUser, true);
        Require(!broken.User && broken.Global, "a missing personal URL blocked the wrong list");
        Require(!EvaluateAddonEditability(asAdmin, false).Global, "a non-admin could edit the global list");
        Require(EvaluateAddonEditability({}, false).User, "an empty list could not be added to");
    }

    // Mirrors App.tsx (theme-dark on the title page) and TitleBar's dark player bar.
    void TestShellChrome()
    {
        using HaloDesktop::Shell::ResolveShellChrome;

        auto const browsing = ResolveShellChrome({});
        Require(!browsing.ForceDarkPalette && !browsing.PlayerTitleBar, "a browse page changed the window chrome");

        auto const title = ResolveShellChrome({ .TitlePageCurrent = true });
        Require(title.ForceDarkPalette && !title.PlayerTitleBar, "a title page did not turn the window dark");

        // The player over a title page keeps the window dark beneath it, so
        // closing the player lands back on a dark title page.
        auto const playingTitle = ResolveShellChrome({ .TitlePageCurrent = true, .PlayerOverlayOpen = true });
        Require(playingTitle.ForceDarkPalette && playingTitle.PlayerTitleBar, "the player over a title page lost a chrome rule");

        auto const playingHome = ResolveShellChrome({ .PlayerOverlayOpen = true });
        Require(!playingHome.ForceDarkPalette && playingHome.PlayerTitleBar, "the player did not pin its title bar dark");

        // Sign-in keeps the viewer's own theme even over a title page.
        auto const signIn = ResolveShellChrome({ .TitlePageCurrent = true, .SignInOverlayOpen = true });
        Require(!signIn.ForceDarkPalette && !signIn.PlayerTitleBar, "the sign-in page took the title page's palette");
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
        TestVideoQualityBadges();
        TestQualityMedals();
        TestQualityMedalsFromPicture();
        TestHaloMarkGeometry();
        TestAddonSelection();
        TestTemporaryFileCleanup();
        TestScrubPreviewMapping();
        TestScrubPreviewPlacement();
        TestScrubPreviewCoalescing();
        TestScrubPreviewLoading();
        TestPlaybackTimeFormatting();
        TestReleaseFileMatching();
        TestTransferRateWindow();
        TestActionEntries();
        TestLanguagePolicy();
        TestRatings();
        TestArtworkUrls();
        TestAccountLabel();
        TestAddonListPolicy();
        TestShellChrome();
        std::cout<<"Playback policy tests passed.\n";
        return 0;
    }
    catch(std::exception const&error)
    {
        std::cerr<<"Playback policy tests failed: "<<error.what()<<'\n';
        return 1;
    }
}
