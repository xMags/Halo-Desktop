#include "Services/CatalogRefreshPolicy.h"
#include "Services/Downloads/DownloadPageOperationState.h"
#include "Services/Downloads/DownloadPreparation.h"
#include "ViewModels/HomeStatePolicy.h"
#include "DownloadTransferTest.h"
#include "StorageTests.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
            "Progress",
            "Detail",
        };
        for (auto const property : mutableProperties)
        {
            auto const binding = std::string{ "{x:Bind " } + std::string{ property };
            auto position = xaml.find(binding);
            Require(position != std::string::npos, "a mutable download row property was not bound");
            while (position != std::string::npos)
            {
                auto const mode = position + binding.size();
                Require(
                    xaml.compare(mode, std::string_view{ ", Mode=OneWay}" }.size(), ", Mode=OneWay}") == 0,
                    "a mutable download row property used a one-time binding");
                position = xaml.find(binding, mode);
            }
        }
    }
} // namespace

int main()
{
    try
    {
        TestCatalogCommitDecisions();
        TestCatalogDirtySingleFlight();
        TestHomeVisibility();
        TestFilteredFeaturedSelection();
        TestSelectFeaturedItems();
        TestOptionalSubtitleFallback();
        TestDownloadPageOperationLifetime();
        TestMutableDownloadRowBindings();
        RunStandaloneStorageTests();
        RunDownloadTransferStabilityTest();
        std::cout << "StabilityTests passed\n";
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << "StabilityTests failed: " << error.what() << '\n';
        return 1;
    }
}
