#include <winsock2.h>
#include <ws2tcpip.h>

#include "Services/CatalogRefreshPolicy.h"
#include "Api/Dto.h"
#include "Api/JsonNumberPolicy.h"
#include "Services/SettingsSyncPolicy.h"
#include "Services/DiscordPresence.h"
#include "Services/Downloads/DownloadPageOperationState.h"
#include "Services/Downloads/DownloadTypes.h"
#include "Services/Downloads/DownloadPreparation.h"
#include "Services/Auth/LoopbackListener.h"
#include "Services/StreamInfo.h"
#include "Services/ContinueShelfPolicy.h"
#include "ViewModels/HomeStatePolicy.h"
#include "DownloadTransferTest.h"
#include "PlaybackSourceResolverTest.h"
#include "StorageTests.h"

#include <cstddef>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    void TestCatalogCommitDecisions()
    {
        Require(
            HaloDesktop::Services::DecideCatalogCommit(0, 0) ==
                HaloDesktop::Services::CatalogCommitDecision::Commit,
            "zero eligible catalogs were not accepted as a valid empty snapshot");
        Require(
            HaloDesktop::Services::DecideCatalogCommit(3, 0) ==
                HaloDesktop::Services::CatalogCommitDecision::PreserveAndFail,
            "an all-failed catalog load replaced the previous snapshot");
        Require(
            HaloDesktop::Services::DecideCatalogCommit(3, 1) ==
                HaloDesktop::Services::CatalogCommitDecision::Commit,
            "a partial catalog success was rejected");
    }

    void TestStreamVideoSizeValidation()
    {
        auto parseSize = [](std::wstring_view value)
        {
            auto const json = std::wstring{
                LR"({"results":[{"addon":{"id":"addon","name":"Addon"},"streams":[{"url":"https://example.test/video","behaviorHints":{"videoSize":)" }
                + std::wstring{ value }
                + LR"(}}]}],"errors":[]})";
            auto const payload = HaloDesktop::Api::Mappers::ParseStreams(
                winrt::Windows::Data::Json::JsonValue::Parse(json));
            Require(payload.Results.size() == 1 && payload.Results.front().Streams.size() == 1,
                "an invalid optional stream size discarded an otherwise playable source");
            return payload.Results.front().Streams.front().VideoSize;
        };

        Require(parseSize(L"123456") == 123456,
            "a valid stream size was not retained");
        Require(!parseSize(L"1.5"),
            "a fractional stream size was truncated into an integer");
        Require(!parseSize(L"1e300"),
            "an out-of-range stream size reached an integer conversion");
        Require(!parseSize(L"18446744073709551616"),
            "2^64 was accepted as a uint64 stream size");
    }

    void TestNumericBoundaryValidation()
    {
        using HaloDesktop::Api::CheckedNonnegativeInt64;
        using HaloDesktop::Api::CheckedPositiveInt64;
        using HaloDesktop::Api::CheckedTokenExpiry;

        Require(CheckedPositiveInt64(9'223'372'036'854'774'784.0).has_value(),
            "the largest safely representable positive int64 double was rejected");
        Require(!CheckedPositiveInt64(9'223'372'036'854'775'808.0).has_value(),
            "2^63 reached the positive int64 conversion");
        Require(!CheckedNonnegativeInt64(9'223'372'036'854'775'808.0).has_value(),
            "2^63 reached the nonnegative int64 conversion");
        Require(CheckedTokenExpiry(60.0, 1'000).value_or(0) == 61'000,
            "a valid token lifetime was rejected");
        Require(!CheckedTokenExpiry(1.5, 1'000).has_value(),
            "a fractional token lifetime was accepted");
        Require(!CheckedTokenExpiry(9'223'372'036'854'775'808.0, 1'000).has_value(),
            "an oversized token lifetime was accepted");
        Require(!CheckedTokenExpiry(60.0, (std::numeric_limits<std::int64_t>::max)() - 59'999).has_value(),
            "a token expiry addition overflow was accepted");

        auto rejected = false;
        try
        {
            static_cast<void>(HaloDesktop::Api::Mappers::ParseMe(
                winrt::Windows::Data::Json::JsonValue::Parse(
                    LR"({"id":"user","username":"name","isAdmin":false,"createdAt":9223372036854775808})")));
        }
        catch (...)
        {
            rejected = true;
        }
        Require(rejected, "an oversized account timestamp was accepted");

        auto const oversizedWatch = HaloDesktop::Api::Mappers::ParseWatchState(
            winrt::Windows::Data::Json::JsonValue::Parse(
                LR"([{"videoId":"video","itemId":"movie:item","positionSec":1,"durationSec":9223372036854775808,"watched":false,"updatedAt":1}])"));
        Require(oversizedWatch.empty(), "an oversized watch duration was accepted");

        auto removalRejected = false;
        try
        {
            static_cast<void>(HaloDesktop::Api::Mappers::ParseLibrary(
                winrt::Windows::Data::Json::JsonValue::Parse(
                    LR"([{"id":"movie:item","type":"movie","name":"Item","addedAt":1,"removedAt":9223372036854775808,"updatedAt":1}])")));
        }
        catch (...)
        {
            removalRejected = true;
        }
        Require(removalRejected, "an oversized library removal timestamp was accepted");

        HaloDesktop::Api::Dto::StreamRecord stream;
        stream.Url = L"https://example.test/video";
        stream.Title = L"9999999999999999999 GB";
        Require(!HaloDesktop::Services::ParseStreamInfo(stream).SizeBytes,
            "an out-of-range textual source size was converted to uint64");
    }

    void TestSettingsLoadPolicy()
    {
        using HaloDesktop::Services::ShouldApplyLoadedSettings;
        Require(!ShouldApplyLoadedSettings(99, 100, 1, 1),
            "an older settings payload replaced a local document");
        Require(!ShouldApplyLoadedSettings(101, 100, 1, 2),
            "a settings payload from before a local write was applied");
        Require(!ShouldApplyLoadedSettings(100, 100, 1, 1),
            "an equal-timestamp settings payload replaced the local document");
        Require(ShouldApplyLoadedSettings(101, 100, 0, 0),
            "a newer remote settings payload was rejected");
    }

    class FakeDiscordTransport final : public HaloDesktop::Services::IDiscordPresenceTransport
    {
    public:
        [[nodiscard]] bool Send(
            std::wstring const& applicationId,
            std::string const& payload) noexcept override
        {
            std::scoped_lock const lock{ Mutex };
            ApplicationIds.push_back(applicationId);
            Payloads.push_back(payload);
            Changed.notify_all();
            if (FailuresRemaining > 0)
            {
                --FailuresRemaining;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool WaitForCount(std::size_t count)
        {
            std::unique_lock lock{ Mutex };
            return Changed.wait_for(lock, std::chrono::seconds{ 2 }, [this, count]
            {
                return Payloads.size() >= count;
            });
        }

        std::mutex Mutex;
        std::condition_variable Changed;
        std::vector<std::wstring> ApplicationIds;
        std::vector<std::string> Payloads;
        int FailuresRemaining{};
    };

    HaloDesktop::Playback::PlaybackState PresenceState()
    {
        return {
            .PositionSeconds = 120.0,
            .DurationSeconds = 3600.0,
            .Speed = 1.0,
            .FileSerial = 7,
            .SeekSerial = 2,
        };
    }

    void TestDiscordPresencePolicyAndService()
    {
        using HaloDesktop::Services::BuildPresenceActivity;
        using HaloDesktop::Services::DiscordPresenceService;
        using HaloDesktop::Services::PresenceMedia;
        using HaloDesktop::Services::PresencePlaybackState;
        using HaloDesktop::Services::SerializeClearActivity;
        using HaloDesktop::Services::SerializeSetActivity;

        auto const capturedAt = std::chrono::system_clock::time_point{ std::chrono::seconds{ 2'000 } };
        auto state = PresenceState();
        auto movie = BuildPresenceActivity(
            { L"Arrival", L"Arrival", L"", L"movie", L"https://images.example.com/posters/arrival.jpg" },
            state,
            capturedAt);
        Require(movie.has_value(), "a playing movie did not produce presence");
        Require(movie->Details == L"Arrival" && movie->State == L"Movie",
            "movie presence did not contain its title and media kind");
        auto movieJson = SerializeSetActivity(*movie, 42);
        Require(movieJson.find("\"start\":1880") != std::string::npos,
            "elapsed playback time was not anchored to the captured position");
        Require(movieJson.find("\"end\":5480") != std::string::npos,
            "remaining playback time was not anchored to the captured duration");
        Require(movieJson.find("https://images.example.com/posters/arrival.jpg") != std::string::npos,
            "the public movie poster was not used as Discord artwork");
        Require(movieJson.find("\"type\":3") != std::string::npos,
            "Discord presence was not identified as Watching");

        constexpr wchar_t const* unsafePosters[]{
            L"http://images.example.com/poster.jpg",
            L"https://user:password@images.example.com/poster.jpg",
            L"https://images.example.com/poster.jpg?token=secret",
            L"https://images.example.com/poster.jpg#fragment",
            L"https://127.0.0.1/poster.jpg",
            L"https://192.168.1.2/poster.jpg",
            L"https://poster.local/poster.jpg",
            L"file:///C:/poster.jpg",
        };
        for (auto const* unsafePoster : unsafePosters)
        {
            auto fallback = BuildPresenceActivity(
                { L"Arrival", L"Arrival", L"", L"movie", unsafePoster }, state, capturedAt);
            Require(fallback && fallback->ArtworkUrl.empty(),
                "an unsafe poster URL crossed the Discord presence boundary");
            auto const fallbackJson = SerializeSetActivity(*fallback, 42);
            Require(fallbackJson.find("\"large_image\":\"halo\"") != std::string::npos,
                "unsafe poster artwork did not fall back to the Halo asset");
        }

        auto episode = BuildPresenceActivity(
            { L"The Ones Who Live", L"The Walking Dead", L"S01E06", L"series" }, state, capturedAt);
        Require(episode.has_value(), "a playing episode did not produce presence");
        Require(episode->Details == L"The Walking Dead"
                && episode->State == L"S01E06 \u00b7 The Ones Who Live",
            "episode presence did not contain the series, episode, and title");

        state.Paused = true;
        auto paused = BuildPresenceActivity({ L"Arrival", L"", L"" }, state, capturedAt);
        Require(paused && paused->Playback == PresencePlaybackState::Paused,
            "paused playback was not represented as paused");
        auto pausedJson = SerializeSetActivity(*paused, 42);
        Require(pausedJson.find("Paused") != std::string::npos
                && pausedJson.find("timestamps") == std::string::npos,
            "paused presence showed moving timestamps or omitted its state");

        state.Paused = false;
        state.Buffering = true;
        auto buffering = BuildPresenceActivity({ L"Arrival", L"", L"" }, state, capturedAt);
        auto bufferingJson = SerializeSetActivity(*buffering, 42);
        Require(bufferingJson.find("Buffering") != std::string::npos
                && bufferingJson.find("timestamps") == std::string::npos,
            "buffering presence showed moving timestamps or omitted its state");

        state = PresenceState();
        std::wstring invalidTitle(200, L'x');
        invalidTitle[2] = static_cast<wchar_t>(0xd800);
        invalidTitle[3] = L'\n';
        auto sanitized = BuildPresenceActivity({ invalidTitle, L"", L"" }, state, capturedAt);
        Require(sanitized && winrt::to_string(winrt::hstring{ sanitized->Details }).size() <= 128,
            "Discord text exceeded its UTF-8 byte limit");
        auto sanitizedJson = SerializeSetActivity(*sanitized, 42);
        Require(sanitizedJson.find("https://private.invalid") == std::string::npos
                && sanitizedJson.find("Authorization") == std::string::npos
                && sanitizedJson.find("addon-secret") == std::string::npos,
            "presence serialization admitted data outside the display-safe contract");
        Require(SerializeClearActivity(42).find("\"activity\":null") != std::string::npos,
            "clearing presence did not send a null activity");

        auto transport = std::make_shared<FakeDiscordTransport>();
        {
            DiscordPresenceService service{ false, transport, std::chrono::milliseconds{ 5 } };
            service.SetMedia({ L"Arrival", L"", L"" });
            service.Update(PresenceState());
            Require(!transport->WaitForCount(1), "disabled Rich Presence published activity");
            service.SetEnabled(true);
            Require(transport->WaitForCount(1), "enabling Rich Presence did not publish current playback");
            {
                std::scoped_lock const lock{ transport->Mutex };
                Require(transport->ApplicationIds.front() == L"1544266293249712128",
                    "the Discord transport received the wrong Application ID");
            }
            service.SetEnabled(false);
            Require(transport->WaitForCount(2), "disabling Rich Presence did not clear current playback");
            {
                std::scoped_lock const lock{ transport->Mutex };
                Require(transport->Payloads.back().find("\"activity\":null") != std::string::npos,
                    "disabling Rich Presence did not send a null activity");
            }
            service.SetEnabled(true);
            service.Update(PresenceState());
            Require(transport->WaitForCount(3), "re-enabling Rich Presence did not replay current playback");
            service.Clear();
            Require(transport->WaitForCount(4), "clearing Rich Presence did not reach the transport");
            std::scoped_lock const lock{ transport->Mutex };
            Require(transport->Payloads.back().find("\"activity\":null") != std::string::npos,
                "the service clear operation did not send a null activity");
        }

        auto localFileTransport = std::make_shared<FakeDiscordTransport>();
        {
            DiscordPresenceService service{ true, localFileTransport, std::chrono::milliseconds{ 5 } };
            service.SetMedia({
                L"Spider-Man: No Way Home",
                L"Spider-Man: No Way Home",
                L"",
                L"movie",
                L"https://images.example.com/posters/spider-man.jpg" });
            auto loading = PresenceState();
            loading.FileSerial = 0;
            loading.Buffering = true;
            service.Update(loading);
            Require(!localFileTransport->WaitForCount(1),
                "pre-load local playback published an invalid activity");
            service.Update(PresenceState());
            Require(localFileTransport->WaitForCount(1),
                "a local file lost its metadata before FILE_LOADED");
            std::scoped_lock const lock{ localFileTransport->Mutex };
            Require(localFileTransport->Payloads.back().find("Spider-Man: No Way Home") != std::string::npos,
                "local file presence did not publish its title");
            Require(localFileTransport->Payloads.back().find("https://images.example.com/posters/spider-man.jpg") != std::string::npos,
                "local file presence did not publish its stored poster");
        }

        auto retryTransport = std::make_shared<FakeDiscordTransport>();
        retryTransport->FailuresRemaining = 1;
        auto const shutdownStart = std::chrono::steady_clock::now();
        {
            DiscordPresenceService service{ true, retryTransport, std::chrono::milliseconds{ 5 } };
            service.SetMedia({ L"Arrival", L"", L"" });
            service.Update(PresenceState());
            Require(retryTransport->WaitForCount(2), "a failed Discord connection was not retried");
        }
        Require(std::chrono::steady_clock::now() - shutdownStart < std::chrono::seconds{ 2 },
            "Discord worker shutdown blocked application teardown");
    }

    void TestCatalogDirtySingleFlight()
    {
        HaloDesktop::Services::CatalogRefreshState state;
        Require(!state.HasLoaded() && state.IsDirty(), "initial catalog state was not refreshable");

        auto const first = state.TryBeginRefresh();
        Require(first.has_value(), "initial catalog refresh could not begin");
        Require(!state.TryBeginRefresh().has_value(), "a competing catalog refresh was admitted");

        state.Invalidate();
        state.CompleteRefresh(*first, true);
        Require(state.HasLoaded(), "a committed catalog snapshot was not marked loaded");
        Require(state.IsDirty(), "an in-flight refresh cleared a newer invalidation");

        auto const second = state.TryBeginRefresh();
        Require(second.has_value(), "the newer catalog invalidation was not retryable");
        state.CompleteRefresh(*second, true);
        Require(!state.IsDirty(), "the current catalog refresh did not clear dirty state");

        state.Invalidate();
        auto const failed = state.TryBeginRefresh();
        Require(failed.has_value(), "a dirty catalog refresh could not begin");
        state.CompleteRefresh(*failed, false);
        Require(state.IsDirty() && !state.RefreshInFlight(), "a failed refresh did not remain retryable");
    }

    void TestHomeVisibility()
    {
        auto const retained = HaloDesktop::ViewModels::ResolveHomeVisibility(true, false, true);
        Require(retained.ShowContent && !retained.ShowLoading,
                "existing Home content was hidden by a refresh");

        auto const continueOnly = HaloDesktop::ViewModels::ResolveHomeVisibility(false, false, true);
        Require(continueOnly.ShowContent && !continueOnly.ShowEmpty,
                "Continue Watching overlapped the empty state");

        auto const empty = HaloDesktop::ViewModels::ResolveHomeVisibility(false, false, false);
        Require(empty.ShowEmpty && !empty.ShowContent, "a valid empty Home snapshot was not shown as empty");

        auto const failed = HaloDesktop::ViewModels::ResolveHomeVisibility(false, true, false);
        Require(failed.ShowError && !failed.ShowEmpty, "an initial Home failure was presented as empty");
    }

    void TestFilteredFeaturedSelection()
    {
        std::vector<HaloDesktop::ViewModels::HomeMediaKind> const kinds{
            HaloDesktop::ViewModels::HomeMediaKind::Series,
            HaloDesktop::ViewModels::HomeMediaKind::Movie,
            HaloDesktop::ViewModels::HomeMediaKind::Series,
        };

        auto const all = HaloDesktop::ViewModels::FirstMatchingHomeItem(
            HaloDesktop::ViewModels::HomeFilter::All, kinds);
        auto const movie = HaloDesktop::ViewModels::FirstMatchingHomeItem(
            HaloDesktop::ViewModels::HomeFilter::Movies, kinds);
        auto const series = HaloDesktop::ViewModels::FirstMatchingHomeItem(
            HaloDesktop::ViewModels::HomeFilter::Series, kinds);
        Require(all && *all == 0, "the All filter did not select the first catalog title");
        Require(movie && *movie == 1, "the Movies filter did not select the first movie");
        Require(series && *series == 0, "the Series filter did not select the first series");

        std::vector<HaloDesktop::ViewModels::HomeMediaKind> const moviesOnly{
            HaloDesktop::ViewModels::HomeMediaKind::Movie,
        };
        Require(
            !HaloDesktop::ViewModels::FirstMatchingHomeItem(
                HaloDesktop::ViewModels::HomeFilter::Series, moviesOnly),
            "a filtered hero was fabricated when no catalog title matched");
    }

    // Backdrops lead, order is catalog order inside each group, keys never
    // repeat and unaddressable titles never earn a slot.
    void TestSelectFeaturedItems()
    {
        using HaloDesktop::ViewModels::FeaturedCandidate;
        using HaloDesktop::ViewModels::HomeFilter;
        using HaloDesktop::ViewModels::HomeMediaKind;
        using HaloDesktop::ViewModels::SelectFeaturedItems;

        std::vector<FeaturedCandidate> const candidates{
            { L"movie:a", HomeMediaKind::Movie, false },
            { L"movie:b", HomeMediaKind::Movie, true },
            { L"series:c", HomeMediaKind::Series, true },
            { L"movie:a", HomeMediaKind::Movie, true }, // same title again, higher in rank
            { L"movie:d", HomeMediaKind::Movie, false },
            { L"", HomeMediaKind::Movie, true }, // unaddressable: no type:id to open
            { L"series:e", HomeMediaKind::Series, false },
        };

        auto const allTwo = SelectFeaturedItems(HomeFilter::All, candidates, 2);
        Require(allTwo.size() == 2 && allTwo[0] == 1 && allTwo[1] == 2,
            "the featured strip did not lead with the top backdrops in catalog order");

        // index 3 is the duplicate of index 0, index 5 has an empty key, so the
        // fifth slot is the last non-backdrop in catalog order, not a repeat.
        auto const allFive = SelectFeaturedItems(HomeFilter::All, candidates, 5);
        Require(allFive == std::vector<std::size_t>({ 1, 2, 3, 4, 6 }),
            "non-backdrops did not top up the strip in catalog order, or a duplicate took a slot");

        auto const movies = SelectFeaturedItems(HomeFilter::Movies, candidates, 10);
        Require(movies == std::vector<std::size_t>({ 1, 3, 4 }),
            "a non-movie leaked into the Movies strip");

        auto const series = SelectFeaturedItems(HomeFilter::Series, candidates, 10);
        Require(series == std::vector<std::size_t>({ 2, 6 }),
            "a non-series leaked into the Series strip");

        Require(SelectFeaturedItems(HomeFilter::All, candidates, 0).empty(),
            "a zero-width featured strip was filled");
        Require(SelectFeaturedItems(HomeFilter::All, {}, 5).empty(),
            "a featured strip was fabricated from an empty pool");
    }

    HaloDesktop::Services::Downloads::DownloadStartRequest ProtectedVideoRequest()
    {
        return {
            .Media = {
                .VideoId = L"movie:test",
                .ItemId = L"movie:test",
                .MediaType = L"movie",
                .Title = L"Test movie",
            },
            .Request = {
                .Url = L"https://video.invalid/protected",
                .Headers = { { L"Authorization", L"video-secret" } },
            },
        };
    }

    void TestOptionalSubtitleFallback()
    {
        constexpr std::string_view failureStages[]{ "hash", "lookup", "proxy" };
        for (auto const failureStage : failureStages)
        {
            auto prepared = HaloDesktop::Services::Downloads::
                PrepareDownloadWithOptionalSubtitleAsync(
                    ProtectedVideoRequest(),
                    [failureStage]()
                    {
                        return concurrency::create_task([failureStage]()
                            -> std::optional<HaloDesktop::Services::Downloads::SubtitleRequest>
                        {
                            throw std::runtime_error{ std::string{ failureStage } };
                        });
                    }).get();
            Require(!prepared.Request.Subtitle, "a failed subtitle preparation left a sidecar attached");
            Require(
                prepared.Request.Url == L"https://video.invalid/protected",
                "a subtitle failure changed the protected video URL");
            Require(
                prepared.Request.Headers.at(L"Authorization") == L"video-secret",
                "a subtitle failure changed the protected video headers");
        }

        auto prepared = HaloDesktop::Services::Downloads::PrepareDownloadWithOptionalSubtitleAsync(
            ProtectedVideoRequest(),
            []()
            {
                return concurrency::task_from_result(
                    std::optional<HaloDesktop::Services::Downloads::SubtitleRequest>{ {
                        .Url = L"https://subtitle.invalid/protected",
                        .Language = L"eng",
                        .Id = L"subtitle:test",
                        .Headers = { { L"Authorization", L"subtitle-secret" } },
                    } });
            }).get();
        Require(prepared.Request.Subtitle.has_value(), "a prepared subtitle sidecar was discarded");
        Require(
            prepared.Request.Subtitle->Headers.at(L"Authorization") == L"subtitle-secret",
            "protected subtitle headers were not preserved");
    }

    void TestDownloadPageOperationLifetime()
    {
        HaloDesktop::Services::Downloads::DownloadPageOperationState state;
        state.NavigatedTo();
        Require(!state.TryBegin(), "an unloaded Sources page admitted a download action");
        state.Loaded();

        auto const first = state.TryBegin();
        Require(first.has_value(), "a loaded Sources page rejected its first download action");
        Require(!state.TryBegin(), "competing download preparation was admitted");
        Require(state.CanApply(*first), "the current download operation was treated as stale");

        state.Unloaded();
        Require(!state.CanApply(*first), "a navigated-away Sources page could still apply completion");
        state.Complete(*first);
        Require(!state.InFlight(), "a stale completed operation left the page permanently busy");

        state.NavigatedTo();
        state.Loaded();
        auto const second = state.TryBegin();
        Require(second.has_value(), "a reloaded Sources page could not start a new operation");
        Require(!state.CanApply(*first), "an old page operation matched a newer page generation");
        Require(state.CanApply(*second), "the newer page operation was not current");
        state.Complete(*second);
    }

    void TestMutableDownloadRowBindings()
    {
        auto xamlPath = std::filesystem::path{ L"DownloadsPage.xaml" };
        if (!std::filesystem::is_regular_file(xamlPath))
        {
            xamlPath = L"src/HaloDesktop/Views/DownloadsPage.xaml";
        }
        std::ifstream input{ xamlPath, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{ "the Downloads page XAML test resource was unavailable" };
        }
        auto const xaml = std::string{
            std::istreambuf_iterator<char>{ input },
            std::istreambuf_iterator<char>{} };
        constexpr std::string_view mutableProperties[]{
            "Poster",
            "Tag",
            "Name",
            "Sub",
            "DownloadingVisibility",
            "QueuedVisibility",
            "PausedVisibility",
            "FailedVisibility",
            "PauseVisibility",
            "ResumeVisibility",
            "RetryVisibility",
            "ChooseSourceVisibility",
            "Progress",
            "Detail",
            "DownloadedLine",
            "QualityBadgeTier",
            "QualityBadgeDetail",
            "QualityTone",
            "SubsChip",
            "SubsNormalVisibility",
            "SubsMutedVisibility",
            "AddedLabel",
            "LeadNormalVisibility",
            "LeadCautionVisibility",
            "LeadCriticalVisibility",
            "ProgressAccentVisibility",
            "ProgressCautionVisibility",
            "ProgressCriticalVisibility",
        };
        for (auto const property : mutableProperties)
        {
            auto const binding = std::string{ "{x:Bind " } + std::string{ property };
            auto position = xaml.find(binding);
            auto foundExact = false;
            while (position != std::string::npos)
            {
                auto const mode = position + binding.size();
                if (mode < xaml.size() && (xaml[mode] == ',' || xaml[mode] == '}'))
                {
                    foundExact = true;
                    Require(
                        xaml.compare(mode, std::string_view{ ", Mode=OneWay}" }.size(), ", Mode=OneWay}") == 0,
                        "a mutable download row property used a one-time binding");
                }
                position = xaml.find(binding, mode);
            }
            Require(foundExact, "a mutable download row property was not bound");
        }
    }

    void TestBrowserSignInListenerCancellation()
    {
        auto cancelAndAwait = []()
        {
            HaloDesktop::Services::Auth::LoopbackListener listener{ std::chrono::seconds{ 2 } };
            auto pending = listener.WaitAsync();
            listener.Cancel();
            auto cancelled = false;
            try
            {
                static_cast<void>(pending.get());
            }
            catch (std::runtime_error const&)
            {
                cancelled = true;
            }
            Require(cancelled, "browser sign-in cancellation did not complete its listener");
        };

        cancelAndAwait();
        cancelAndAwait();

        HaloDesktop::Services::Auth::LoopbackListener listener{ std::chrono::seconds{ 5 } };
        auto pending = listener.WaitAsync();
        auto const stalledClient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Require(stalledClient != INVALID_SOCKET,
            "the cancellation test could not create a stalled loopback client");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(17871);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Require(connect(stalledClient, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != SOCKET_ERROR,
            "the cancellation test could not occupy the loopback listener");
        auto const started = std::chrono::steady_clock::now();
        listener.Cancel();
        auto const elapsed = std::chrono::steady_clock::now() - started;
        closesocket(stalledClient);
        Require(elapsed < std::chrono::seconds{ 2 },
            "browser sign-in cancellation waited for a stalled loopback client");
        auto stalledCancelled = false;
        try
        {
            static_cast<void>(pending.get());
        }
        catch (std::runtime_error const&)
        {
            stalledCancelled = true;
        }
        Require(stalledCancelled,
            "a stalled browser sign-in listener completed successfully after cancellation");
    }
    void TestEpisodePositionParsing()
    {
        using HaloDesktop::Services::Downloads::ParseEpisodePosition;

        auto const at = [](int season, int episode) { return std::optional{ std::pair{ season, episode } }; };

        Require(ParseEpisodePosition(L"S01E05") == at(1, 5), "the label form we write did not parse");
        Require(ParseEpisodePosition(L"S10E05") == at(10, 5), "a two digit season did not parse");

        // The label is written with a minimum width, not a fixed one, so a show
        // past its hundredth episode writes more digits than the season does.
        Require(ParseEpisodePosition(L"S01E100") == at(1, 100), "a three digit episode did not parse");
        Require(ParseEpisodePosition(L"S01E1085") == at(1, 1085), "a four digit episode did not parse");
        Require(ParseEpisodePosition(L"S100E01") == at(100, 1), "a three digit season did not parse");

        // Ordering is what selects the next episode, so it has to hold across a
        // change in digit count rather than comparing the labels as text.
        Require(ParseEpisodePosition(L"S01E99") < ParseEpisodePosition(L"S01E100"), "episode 100 did not sort after episode 99");
        Require(ParseEpisodePosition(L"S01E100") < ParseEpisodePosition(L"S02E01"), "the next season did not sort after this one");

        Require(ParseEpisodePosition(L"s01e05") == at(1, 5), "a lowercase label did not parse");
        Require(ParseEpisodePosition(L"S1E5") == at(1, 5), "an unpadded label did not parse");
        Require(ParseEpisodePosition(L"S01 E05") == at(1, 5), "a spaced label did not parse");
        Require(ParseEpisodePosition(L" S01E05 ") == at(1, 5), "a padded label did not parse");
        Require(ParseEpisodePosition(L"S00E01") == at(0, 1), "a specials season did not parse");

        Require(!ParseEpisodePosition(std::nullopt), "a missing label parsed");
        Require(!ParseEpisodePosition(L""), "an empty label parsed");
        Require(!ParseEpisodePosition(L"S01"), "a season without an episode parsed");
        Require(!ParseEpisodePosition(L"E05"), "an episode without a season parsed");
        Require(!ParseEpisodePosition(L"SE"), "a label with no numbers parsed");
        Require(!ParseEpisodePosition(L"S01E05x"), "trailing text was ignored");
        Require(!ParseEpisodePosition(L"Season 1"), "free text parsed");
        // Refused rather than truncated: a run this long is not an episode number,
        // and accepting it would be accepting an overflow.
        Require(!ParseEpisodePosition(L"S01E123456"), "an implausibly long number parsed");
    }

    HaloDesktop::Services::ContinueWatchRow WatchRow(
        wchar_t const* itemId,
        wchar_t const* videoId,
        wchar_t const* name,
        double position,
        double duration,
        bool watched,
        std::int64_t updatedAt)
    {
        return { itemId, videoId, name, L"poster.jpg", position, duration, watched, updatedAt };
    }

    void TestContinueShelf()
    {
        using HaloDesktop::Services::BuildContinueShelf;
        using HaloDesktop::Services::ContinueCardKind;
        using HaloDesktop::Services::ContinueWatchRow;
        using HaloDesktop::Services::IsFinishedWatchRow;
        using HaloDesktop::Services::MetaIdFromItemId;
        using HaloDesktop::Services::NextEpisodeLookup;
        using HaloDesktop::Services::NextEpisodeState;
        using HaloDesktop::Services::TypeFromItemId;

        Require(TypeFromItemId(L"series:tt5651844") == L"series", "a series item id did not report its type");
        Require(TypeFromItemId(L"tt10872600") == L"movie", "a bare item id was not treated as a film");
        Require(MetaIdFromItemId(L"series:tt5651844") == L"tt5651844", "the meta id was not taken from the item id");
        Require(MetaIdFromItemId(L"tt10872600") == L"tt10872600", "a bare item id lost its meta id");

        // The reporter sets the flag past ninety percent, and the fraction covers a
        // row written by something that did not set it.
        Require(IsFinishedWatchRow(WatchRow(L"series:ttD", L"ttD:1:1", L"D", 960.0, 1000.0, false, 1)),
                "a row past the finished mark was not treated as finished");
        Require(!IsFinishedWatchRow(WatchRow(L"series:ttD", L"ttD:1:1", L"D", 940.0, 1000.0, false, 1)),
                "a row short of the finished mark was treated as finished");

        auto const unresolved = [](std::wstring const&) { return NextEpisodeLookup{}; };

        // The shape that reported this: a series watched through in order with its
        // newest episode finished, a stray mis-click on an older one, one film seen
        // to the end, and one thing genuinely left partway through.
        std::vector<ContinueWatchRow> const rows{
            WatchRow(L"series:tt5651844", L"tt5651844:3:3", L"Travelers", 2735.0, 2860.0, true, 1000),
            WatchRow(L"movie:tt10872600", L"tt10872600", L"Spider-Man", 9415.0, 9415.0, true, 950),
            WatchRow(L"series:tt5651844", L"tt5651844:2:7", L"Travelers", 49.0, 2683.0, false, 900),
            WatchRow(L"series:tt14688458", L"tt14688458:1:1", L"Silo", 332.0, 3563.0, false, 800),
        };

        auto const cold = BuildContinueShelf(rows, unresolved, 8, 8);
        Require(cold.Cards.size() == 1, "the shelf showed something before the lookup answered");
        Require(cold.Cards[0].Name == L"Silo", "the in-progress row was lost");
        Require(cold.Requests.size() == 1, "the finished series did not produce exactly one request");
        Require(cold.Requests[0].Type == L"series", "the request carried the wrong type");
        Require(cold.Requests[0].MetaId == L"tt5651844", "the request carried the wrong meta id");
        Require(cold.Requests[0].VideoId == L"tt5651844:3:3",
                "the request did not step past the newest finished episode");

        auto const warm = BuildContinueShelf(
            rows,
            [](std::wstring const& key)
            {
                return key == L"tt5651844:3:3"
                    ? NextEpisodeLookup{ NextEpisodeState::Resolved, L"tt5651844:3:4" }
                    : NextEpisodeLookup{};
            },
            8,
            8);
        Require(warm.Cards.size() == 2, "the resolved shelf did not gain the promoted card");
        Require(warm.Cards[0].Name == L"Travelers", "the promoted card did not sort by its finished row");
        Require(warm.Cards[0].Kind == ContinueCardKind::NextEpisode, "the promoted card was not marked as one");
        Require(warm.Cards[0].VideoId == L"tt5651844:3:4", "the promoted card did not point at the next episode");
        Require(warm.Cards[0].PositionSec == 0.0 && warm.Cards[0].DurationSec == 0.0,
                "an episode that was never started carried progress");
        Require(warm.Cards[1].Name == L"Silo" && warm.Cards[1].Kind == ContinueCardKind::Resume,
                "the resume card did not survive promotion happening above it");
        Require(warm.Requests.empty(), "a series that was already resolved was asked about again");

        // The finished episode answers for its show whether or not it draws
        // anything, so the older mis-click cannot speak in its place.
        auto const finale = BuildContinueShelf(
            rows,
            [](std::wstring const&) { return NextEpisodeLookup{ NextEpisodeState::None, L"" }; },
            8,
            8);
        Require(finale.Cards.size() == 1 && finale.Cards[0].Name == L"Silo",
                "a series stayed on the shelf after its last episode");
        Require(finale.Requests.empty(), "a series known to have ended was asked about again");

        // A row that cannot be drawn, one with no duration, and one barely opened.
        // None of them may produce a card or a lookup.
        std::vector<ContinueWatchRow> const unusable{
            WatchRow(L"series:ttA", L"ttA:1:1", L"", 100.0, 1000.0, false, 50),
            WatchRow(L"movie:ttB", L"ttB", L"No duration", 100.0, 0.0, false, 40),
            WatchRow(L"movie:ttC", L"ttC", L"Barely opened", 10.0, 1000.0, false, 30),
        };
        auto const skipped = BuildContinueShelf(unusable, unresolved, 8, 8);
        Require(skipped.Cards.empty(), "an unusable row produced a card");
        Require(skipped.Requests.empty(), "an unusable row produced a lookup");

        // A full shelf still leads with the promoted card, because the walk is in
        // newest-first order and the cap only trims the tail.
        std::vector<ContinueWatchRow> many{
            WatchRow(L"series:tt9000000", L"tt9000000:1:1", L"Newest", 600.0, 610.0, true, 100),
        };
        for (int index = 0; index < 10; ++index)
        {
            auto const suffix = std::to_wstring(index);
            many.push_back({ L"movie:tt" + suffix, L"tt" + suffix, L"Film " + suffix, L"", 100.0, 1000.0, false, 90 - index });
        }
        auto const capped = BuildContinueShelf(
            many,
            [](std::wstring const&) { return NextEpisodeLookup{ NextEpisodeState::Resolved, L"tt9000000:1:2" }; },
            8,
            8);
        Require(capped.Cards.size() == 8, "the shelf exceeded its cap");
        Require(capped.Cards[0].Kind == ContinueCardKind::NextEpisode,
                "the promoted card lost the front of a full shelf");
        Require(capped.Cards[7].Name == L"Film 6", "the cap trimmed from the wrong end");
    }

} // namespace

int main()
{
    try
    {
        TestCatalogCommitDecisions();
        TestStreamVideoSizeValidation();
        TestNumericBoundaryValidation();
        TestSettingsLoadPolicy();
        TestDiscordPresencePolicyAndService();
        TestCatalogDirtySingleFlight();
        TestHomeVisibility();
        TestFilteredFeaturedSelection();
        TestSelectFeaturedItems();
        TestOptionalSubtitleFallback();
        TestContinueShelf();
        TestEpisodePositionParsing();
        TestDownloadPageOperationLifetime();
        TestMutableDownloadRowBindings();
        TestBrowserSignInListenerCancellation();
        RunStandaloneStorageTests();
        RunDownloadTransferStabilityTest();
        RunPlaybackSourceResolverTest();
        std::cout << "StabilityTests passed\n";
        return 0;
    }
    catch (winrt::hresult_error const& error)
    {
        std::wcerr << L"StabilityTests failed with HRESULT 0x"
                   << std::hex << static_cast<std::uint32_t>(error.code().value)
                   << L": " << error.message().c_str() << L'\n';
        return 1;
    }
    catch (std::exception const& error)
    {
        std::cerr << "StabilityTests failed: " << error.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "StabilityTests failed with an unknown exception.\n";
        return 1;
    }
}
