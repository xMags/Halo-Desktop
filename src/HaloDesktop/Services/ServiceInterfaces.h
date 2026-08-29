#pragma once

#include "Api/Dto.h"
#include "Services/Downloads/DownloadTypes.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <vector>
#include <winrt/HaloDesktop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

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

    using CatalogChangedToken = std::uint64_t;
    using CatalogChangedHandler = std::function<void()>;

    class ICatalogService
    {
    public:
        virtual ~ICatalogService() = default;
        [[nodiscard]] virtual concurrency::task<void> LoadAsync() = 0;
        // True once a usable snapshot exists, so a caller can adopt what is already
        // in memory instead of fetching the catalogs a second time.
        [[nodiscard]] virtual bool HasLoaded() const = 0;
        // Advances every time a snapshot worth showing replaces the last one, so a
        // view holding an older one can tell without comparing its contents.
        [[nodiscard]] virtual std::uint64_t SnapshotVersion() const = 0;
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
        [[nodiscard]] virtual concurrency::task<void> SearchAsync(winrt::hstring query) = 0;
        [[nodiscard]] virtual winrt::HaloDesktop::MediaSummary Hero() const = 0;
        [[nodiscard]] virtual winrt::HaloDesktop::MediaSummary FeaturedForFilter(
            std::int32_t filterIndex) const = 0;
        // Up to count distinct titles for the home carousel, in catalog order.
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary>
            FeaturedSetForFilter(std::int32_t filterIndex, std::uint32_t count) const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> ContinueWatching() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> Shelves() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> LibraryItems() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SearchGroup> SearchResults() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> RecentTerms() const = 0;
        virtual void RecordRecent(winrt::hstring term) = 0;
        // Raised when the continue row changes on its own, which happens when a
        // next-episode lookup lands after the snapshot was already shown. Without
        // it a promoted card would not appear until Home was navigated to again.
        [[nodiscard]] virtual CatalogChangedToken AddContinueChangedHandler(
            CatalogChangedHandler handler) = 0;
        virtual void RemoveContinueChangedHandler(CatalogChangedToken token) noexcept = 0;
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
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Episode> Episodes(std::int32_t season) const = 0;
    };

    class ISourceService
    {
    public:
        virtual ~ISourceService() = default;
        [[nodiscard]] virtual concurrency::task<void> LoadAsync(winrt::HaloDesktop::SourcesNavParams const& parameters) = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> Groups() const = 0;
        [[nodiscard]] virtual winrt::HaloDesktop::PlaybackRequest BuildPlaybackRequest(winrt::hstring const& key) const = 0;
        [[nodiscard]] virtual concurrency::task<DownloadStartOutcome> StartDownloadAsync(
            winrt::hstring key,
            bool replaceExisting) = 0;
        [[nodiscard]] virtual winrt::hstring ResolveSummary() const = 0;
        // The files already on this device for the resolve just loaded. Kept apart
        // from Groups() because a download is not a provider: it answered nothing,
        // and counting it as one would make the failure banner misreport how many
        // providers were reached.
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::StreamSource> LocalSources() const = 0;
        virtual void RefreshDownloadStates() = 0;
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
        [[nodiscard]] virtual concurrency::task<DownloadStartOutcome> StartDownloadAsync(
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
        [[nodiscard]] virtual std::int32_t ActiveCount() const noexcept = 0;
        [[nodiscard]] virtual double AggregateRate() const noexcept = 0;
        [[nodiscard]] virtual winrt::hstring QueueLine() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> Transfers() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> Ready() const = 0;
        [[nodiscard]] virtual winrt::Windows::Foundation::Collections::IObservableVector<double> Throughput() const = 0;
        [[nodiscard]] virtual std::uint64_t StoredBytes() const noexcept = 0;
        [[nodiscard]] virtual std::uint64_t InFlightBytes() const noexcept = 0;
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
        [[nodiscard]] virtual bool CanEditLists() const noexcept = 0;
        [[nodiscard]] virtual concurrency::task<void> LoadAsync() = 0;
        [[nodiscard]] virtual concurrency::task<void> AddAsync(winrt::hstring transportUrl) = 0;
        [[nodiscard]] virtual concurrency::task<void> RemoveAsync(winrt::hstring addonId) = 0;
        [[nodiscard]] virtual concurrency::task<void> SetCatalogsVisibleAsync(
            winrt::hstring addonId,
            bool visible) = 0;
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
    };
}
