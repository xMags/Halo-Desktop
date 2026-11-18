#pragma once

#include "Api/Dto.h"
#include "Services/CatalogPagingPolicy.h"
#include "Services/Downloads/DownloadTypes.h"

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <filesystem>
#include <memory>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <string>
#include <vector>
#include <winrt/HaloDesktop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace HaloDesktop::Sources
{
    struct ParsedResolve;
}

namespace HaloDesktop::Services
{
    enum class AuthenticationMode
    {
        Unknown,
        Local,
        Oidc,
    };

    using DownloadChangedToken = std::uint64_t;
    using DownloadChangedHandler = std::function<void()>;

    enum class DownloadStartOutcome
    {
        Started,
        AlreadyExists,
        ReplacementRequired,
        Failed,
    };

    // How a download start went, and for a start the engine refused, its own
    // sentence saying why ("Sign in before downloading.", "There is not enough
    // free space for this video."). The reason is empty for anything that did not
    // come with one, and the caller then says something general.
    struct DownloadStartResult final
    {
        DownloadStartOutcome Outcome{ DownloadStartOutcome::Failed };
        std::wstring Reason;
    };

    using CatalogChangedToken = std::uint64_t;
    using CatalogChangedHandler = std::function<void()>;

    using IdentityChangedToken = std::uint64_t;
    using IdentityChangedHandler = std::function<void()>;

    // Home's carousel: the picks, once they can no longer change as slower
    // catalogs answer. Until then Settled is false and Items is empty.
    struct FeaturedSelection final
    {
        bool Settled{};
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> Items{ nullptr };
    };

    // Where Home's catalog shelves stand as a whole.
    struct HomeCatalogStatus final
    {
        // The shelf list (titles, sources, order) is laid out.
        bool ListKnown{};
        // It holds at least one catalog.
        bool HasCatalogs{};
        // Every shelf's last answer was a failure.
        bool EveryShelfFailed{};
    };

    // A title page's "More like this": the genre it was picked by and the titles.
    struct SimilarShelf final
    {
        winrt::hstring Genre;
        std::vector<winrt::HaloDesktop::MediaSummary> Items;
    };

    // What one /streams request for a video answered, and how long it took.
    struct StreamFetch final
    {
        ::HaloDesktop::Api::Dto::StreamsPayload Payload;
        double ElapsedSeconds{};
    };

    // Whether a resolve may adopt the answer a recent request for the same video
    // already has (or is waiting on), or must ask the addons again.
    enum class StreamReuse
    {
        Recent,
        Fresh,
    };

    class ICatalogService
    {
    public:
        virtual ~ICatalogService() = default;
        // Completes once the addons, the library and the watch history are in and
        // Home's shelf list is laid out; the library and continue projections are
        // final by then. Each catalog shelf then loads on its own, in the
        // background, and the Home changed handlers hear of every one that lands.
        [[nodiscard]] virtual concurrency::task<void> LoadAsync() = 0;
        // True once the shelf list is laid out, so a caller can adopt what is
        // already in memory instead of fetching the catalogs a second time.
        [[nodiscard]] virtual bool HasLoaded() const = 0;
        // Advances every time what Home shows changes (a shelf landing included),
        // so a view holding an older state can tell without comparing contents.
        [[nodiscard]] virtual std::uint64_t SnapshotVersion() const = 0;
        [[nodiscard]] virtual HomeCatalogStatus CatalogStatus() const = 0;
        // Raised on the UI thread whenever SnapshotVersion advances because of a
        // load: the library and continue rows arriving, the shelf list being laid
        // out, a shelf landing. Not raised for the continue or user-state changes
        // that have handlers of their own.
        [[nodiscard]] virtual CatalogChangedToken AddHomeChangedHandler(
            CatalogChangedHandler handler) = 0;
        virtual void RemoveHomeChangedHandler(CatalogChangedToken token) noexcept = 0;
        // Addon mutations invalidate only the catalog portion of Home. A later
        // Home entry owns the refresh, which keeps ordinary navigation network free.
        virtual void InvalidateCatalogs() noexcept = 0;
        [[nodiscard]] virtual bool CatalogsDirty() const noexcept = 0;
        [[nodiscard]] virtual concurrency::task<void> RefreshCatalogsIfDirtyAsync() = 0;
        // Recomputes the continue row on its own. Synchronous and network free: the
        // watch state service holds what the server returned while the player was
        // reporting progress, so the rows behind this are already current.
        virtual void RefreshContinue() = 0;
        // Rebuilds the local library projections after a successful membership
        // mutation without refetching catalogs or any other server state.
        virtual void RebuildLibrary() = 0;
        [[nodiscard]] virtual winrt::HaloDesktop::MediaSummary Hero() const = 0;
        [[nodiscard]] virtual winrt::HaloDesktop::MediaSummary FeaturedForFilter(
            std::int32_t filterIndex) const = 0;
        // Up to count distinct titles for the home carousel, in catalog order,
        // held back until the shelves that have answered fix them.
        [[nodiscard]] virtual FeaturedSelection FeaturedForHome(std::int32_t filterIndex, std::uint32_t count) const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> ContinueWatching() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> Shelves() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> LibraryItems() const = 0;
        // Home's recently watched shelf: the last distinct titles played, newest
        // first, leaving out what the continue row already offers, following
        // the filter. Synchronous and network free.
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary>
            RecentlyWatchedForFilter(std::int32_t filterIndex) const = 0;
        // The page of a catalog after the first `skip` titles, as the addon sent
        // it, for a "See all" grid. Throws when the request fails, and cancels
        // when the account changes before it returns. UI thread in and out.
        [[nodiscard]] virtual concurrency::task<std::vector<winrt::HaloDesktop::MediaSummary>> FetchCatalogPageAsync(
            std::shared_ptr<CatalogPagingSource const> source,
            std::size_t skip) = 0;
        // Raised when the continue row changes on its own, which happens when a
        // next-episode lookup lands after the snapshot was already shown. Without
        // it a promoted card would not appear until Home was navigated to again.
        [[nodiscard]] virtual CatalogChangedToken AddContinueChangedHandler(
            CatalogChangedHandler handler) = 0;
        virtual void RemoveContinueChangedHandler(CatalogChangedToken token) noexcept = 0;
        // For a library or watch-history change made away from the page that shows
        // it, such as from a title's right-click menu: rebuilds the projections the
        // way RebuildLibrary does, then tells every subscriber, so the pages on
        // screen and the pane re-read them. Synchronous and network free.
        virtual void PublishUserStateChange() = 0;
        [[nodiscard]] virtual CatalogChangedToken AddUserStateChangedHandler(
            CatalogChangedHandler handler) = 0;
        virtual void RemoveUserStateChangedHandler(CatalogChangedToken token) noexcept = 0;
        // A title page's "More like this": titles of `type` from the first
        // shown addon catalog that filters by one of `genres` alone, this title
        // and repeats dropped, at most 20. Empty when no catalog can, or it
        // answered nothing. Throws when the request fails, and cancels when the
        // account changes before it returns. UI thread in and out.
        [[nodiscard]] virtual concurrency::task<std::optional<SimilarShelf>> FetchSimilarAsync(
            winrt::hstring type,
            winrt::hstring metaId,
            std::vector<winrt::hstring> genres) = 0;
    };

    class IMetadataService
    {
    public:
        virtual ~IMetadataService() = default;
        [[nodiscard]] virtual concurrency::task<void> LoadAsync(winrt::hstring type,winrt::hstring metaId) = 0;
        [[nodiscard]] virtual winrt::HaloDesktop::MediaDetail Detail() const = 0;
        // Title-level runtime in whole minutes, zero when the addon gave none or
        // the value did not parse. Episodes of a series share the title's value.
        [[nodiscard]] virtual std::int32_t RuntimeMinutes() const noexcept = 0;
        // The same figure for any title, without disturbing the loaded one: answered
        // from it when it is the title asked about, otherwise from the server.
        [[nodiscard]] virtual concurrency::task<std::int32_t> RuntimeMinutesForAsync(
            winrt::hstring type,
            winrt::hstring metaId) = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Episode> Episodes(std::int32_t season) const = 0;
        // The validated meta of the title loaded last, as the server sent it;
        // empty before the first load and after an account change. A copy, so
        // a later load cannot change it under the caller.
        [[nodiscard]] virtual std::optional<::HaloDesktop::Api::Dto::MetaDetail> Meta() const = 0;
    };

    class ISourceService
    {
    public:
        virtual ~ISourceService() = default;
        // Resolves the sheet's sources. With StreamReuse::Recent it adopts the
        // request a title page already made for the same video, finished or still
        // waiting, rather than asking the addons a second time. Release names are
        // parsed off the UI thread. A failed resolve still completes the clear: the
        // previous title's answer is dropped before the failure is rethrown.
        [[nodiscard]] virtual concurrency::task<void> LoadAsync(
            winrt::HaloDesktop::SourcesNavParams const& parameters,
            StreamReuse reuse) = 0;
        // The streams for one video, shared with the sheet: one request answers
        // both. UI thread in and out. Throws when the request fails.
        [[nodiscard]] virtual concurrency::task<StreamFetch> RequestStreamsAsync(
            winrt::hstring type,
            winrt::hstring videoId) = 0;
        // What the last LoadAsync answered, parsed: never null, empty before the
        // first answer and after a failure or an account change. Shared and
        // immutable, so the sheet can rank it without a copy.
        [[nodiscard]] virtual std::shared_ptr<::HaloDesktop::Sources::ParsedResolve const> Current() const = 0;
        // Null for a key the current resolve does not hold. Request headers travel
        // with the request, never into a bound collection.
        [[nodiscard]] virtual winrt::HaloDesktop::PlaybackRequest BuildPlaybackRequest(winrt::hstring const& key) const = 0;
        [[nodiscard]] virtual concurrency::task<DownloadStartResult> StartDownloadAsync(
            winrt::hstring key,
            bool replaceExisting) = 0;
    };

    class IDownloadService
    {
    public:
        virtual ~IDownloadService() = default;
        virtual void Start() = 0;
        virtual void Stop() noexcept = 0;
        virtual void PauseAll() = 0;
        virtual void ResumeAll() = 0;
        virtual bool PauseTransfer(winrt::hstring const& id) = 0;
        virtual bool ResumeTransfer(winrt::hstring const& id) = 0;
        virtual bool CancelTransfer(winrt::hstring const& id) = 0;
        virtual bool DeleteReady(winrt::hstring const& id) = 0;
        virtual void RetryFailedTransfers() = 0;
        virtual bool OpenDownloadDirectory() = 0;
        // Opens Explorer on a finished download's folder with its file selected.
        // A failure is returned rather than left in ActionError, because the caller
        // reports it where it happened instead of on the Downloads page.
        [[nodiscard]] virtual bool RevealInExplorer(winrt::hstring const& id) = 0;
        [[nodiscard]] virtual concurrency::task<DownloadStartResult> StartDownloadAsync(
            Downloads::DownloadStartRequest request) = 0;
        [[nodiscard]] virtual winrt::HaloDesktop::PlaybackRequest BuildPlaybackRequest(
            winrt::hstring const& id) const = 0;
        [[nodiscard]] virtual winrt::HaloDesktop::SourcesNavParams BuildSourcesNavigation(
            winrt::hstring const& id) const = 0;
        [[nodiscard]] virtual std::optional<winrt::HaloDesktop::PlaybackRequest> OfflineNext(
            winrt::hstring const& id) const = 0;
        [[nodiscard]] virtual std::optional<std::filesystem::path> SubtitlePath(
            winrt::hstring const& id) const = 0;
        [[nodiscard]] virtual bool IsRunning() const noexcept = 0;
        [[nodiscard]] virtual bool IsPausedAll() const noexcept = 0;
        [[nodiscard]] virtual bool HasCompleted(winrt::hstring const& videoId) const noexcept = 0;
        // Every finished download of this video, so the sources sheet can offer a
        // local copy without depending on an addon returning the same release again.
        [[nodiscard]] virtual std::vector<Downloads::CompletedDownloadSource> CompletedFor(
            winrt::hstring const& videoId) const = 0;
        // Every download of this video in any state, in index order, leaving out
        // one being deleted. What the sources sheet's saved copy card and save
        // states read.
        [[nodiscard]] virtual std::vector<Downloads::VideoDownload> DownloadsFor(
            winrt::hstring const& videoId) const = 0;
        [[nodiscard]] virtual std::int32_t ActiveCount() const noexcept = 0;
        // The signed-in account's downloads, oldest first, as the Downloads page
        // projects them. UI thread only. No URL or request header is in a record.
        [[nodiscard]] virtual std::vector<Downloads::DownloadRecord> const& Records() const noexcept = 0;
        // Combined transfer rate in MB/s, one sample per chart bar, oldest first.
        [[nodiscard]] virtual std::vector<double> const& Throughput() const noexcept = 0;
        [[nodiscard]] virtual std::optional<std::uint64_t> FreeBytes() const noexcept = 0;
        [[nodiscard]] virtual std::filesystem::path DownloadDirectory() const = 0;
        [[nodiscard]] virtual concurrency::task<void> SetDownloadDirectoryAsync(
            std::filesystem::path directory) = 0;
        [[nodiscard]] virtual winrt::hstring ActionError() const = 0;
        virtual DownloadChangedToken AddChangedHandler(DownloadChangedHandler handler) = 0;
        virtual void RemoveChangedHandler(DownloadChangedToken token) noexcept = 0;
    };

    class IAddonService
    {
    public:
        virtual ~IAddonService() = default;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::Addon> Items() const = 0;
        [[nodiscard]] virtual std::vector<::HaloDesktop::Api::Dto::AddonRecord> Records() const = 0;
        // Whether the caller's own list can be saved back, which needs every
        // personal row's transport URL. Global rows are judged per row.
        [[nodiscard]] virtual bool CanEditLists() const noexcept = 0;
        [[nodiscard]] virtual concurrency::task<void> LoadAsync() = 0;
        [[nodiscard]] virtual concurrency::task<void> AddAsync(winrt::hstring transportUrl) = 0;
        [[nodiscard]] virtual concurrency::task<void> RemoveAsync(winrt::hstring addonId) = 0;
        [[nodiscard]] virtual concurrency::task<void> SetCatalogsVisibleAsync(
            winrt::hstring addonId,
            bool visible) = 0;
        // Re-judges which rows can be edited against the session's admin flag
        // now, rebuilding Items when the answer changed. Cheap and idempotent.
        virtual void RefreshEditability() = 0;
    };

    class ISessionService
    {
    public:
        virtual ~ISessionService() = default;
        [[nodiscard]] virtual winrt::hstring ServerUrl() const = 0;
        [[nodiscard]] virtual winrt::hstring UserId() const = 0;
        [[nodiscard]] virtual winrt::hstring UserName() const = 0;
        [[nodiscard]] virtual bool IsSignedIn() const noexcept = 0;
        [[nodiscard]] virtual bool IsAdmin() const noexcept = 0;
        // The account's tier from a server that has tiers, and the fingerprint of
        // its profile picture. Both are empty until /auth/me has answered for the
        // signed-in account.
        [[nodiscard]] virtual std::optional<winrt::hstring> Plan() const = 0;
        [[nodiscard]] virtual std::optional<winrt::hstring> AvatarVersion() const = 0;
        [[nodiscard]] virtual AuthenticationMode Mode() const noexcept = 0;
        [[nodiscard]] virtual std::uint64_t Generation() const noexcept = 0;
        [[nodiscard]] virtual concurrency::task<void> RestoreAsync() = 0;
        [[nodiscard]] virtual concurrency::task<AuthenticationMode> DiscoverAuthenticationAsync() = 0;
        [[nodiscard]] virtual concurrency::task<winrt::HaloDesktop::SignInOutcome>
            SignInLocalAsync(winrt::hstring username, winrt::hstring password) = 0;
        [[nodiscard]] virtual concurrency::task<winrt::HaloDesktop::SignInOutcome>
            RequestBrowserSignInAsync() = 0;
        virtual void AcknowledgeBrowserSignIn() noexcept = 0;
        virtual void CancelBrowserSignIn() noexcept = 0;
        [[nodiscard]] virtual concurrency::task<std::optional<std::chrono::milliseconds>>
            ProbeHealthAsync() = 0;
        [[nodiscard]] virtual concurrency::task<void> SignOutAsync() = 0;
        // Raised on the UI thread when a sign-in, a sign-out or a rejected session
        // changes which account is signed in, after the services holding account
        // data have been reset for it. A view showing account data reloads here:
        // the page that was on screen when the account changed does not reload by
        // being navigated to again.
        [[nodiscard]] virtual IdentityChangedToken AddIdentityChangedHandler(
            IdentityChangedHandler handler) = 0;
        virtual void RemoveIdentityChangedHandler(IdentityChangedToken token) noexcept = 0;
        // Raised on the UI thread whenever what the account card shows (name,
        // admin flag, tier, picture fingerprint) changes, including when /auth/me
        // refreshes the same account, which raises no identity change. Raised
        // after the identity handlers when both apply.
        [[nodiscard]] virtual IdentityChangedToken AddProfileChangedHandler(
            IdentityChangedHandler handler) = 0;
        virtual void RemoveProfileChangedHandler(IdentityChangedToken token) noexcept = 0;
    };
}
