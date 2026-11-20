#pragma once

#include "Services/Downloads/TransferEngine.h"
#include "Services/DownloadsLogicPolicy.h"
#include "Services/ServiceInterfaces.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace HaloDesktop::Services
{
    class DevicePreferencesStore;
    class DownloadArtworkService;
    class DownloadSidecarFetcher;

    // UI-thread-only facade over the thread-safe transfer engine. Engine
    // callbacks are always marshalled through the captured dispatcher before
    // observable collections or UI callbacks are touched.
    class DownloadService final
        : public IDownloadService,
          public std::enable_shared_from_this<DownloadService>
    {
    public:
        DownloadService(
            std::shared_ptr<Downloads::TransferEngine> engine,
            std::shared_ptr<ISessionService> session,
            std::shared_ptr<DevicePreferencesStore> devicePreferences,
            std::shared_ptr<DownloadArtworkService> artwork,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);
        ~DownloadService() override;

        DownloadService(DownloadService const&) = delete;
        DownloadService& operator=(DownloadService const&) = delete;

        void Start() override;
        void Stop() noexcept override;
        void RebindAccount();
        void PauseAll() override;
        void ResumeAll() override;
        bool PauseTransfer(winrt::hstring const& id) override;
        bool ResumeTransfer(winrt::hstring const& id) override;
        bool CancelTransfer(winrt::hstring const& id) override;
        bool DeleteReady(winrt::hstring const& id) override;
        void RetryFailedTransfers() override;
        std::size_t DeleteAllDownloads() override;
        bool OpenDownloadDirectory() override;
        [[nodiscard]] bool RevealInExplorer(winrt::hstring const& id) override;
        [[nodiscard]] concurrency::task<DownloadStartResult> StartDownloadAsync(
            Downloads::DownloadStartRequest request) override;
        [[nodiscard]] concurrency::task<void> AdoptAccountAsync(std::wstring from, std::wstring to) override;
        [[nodiscard]] winrt::HaloDesktop::PlaybackRequest BuildPlaybackRequest(
            winrt::hstring const& id) const override;
        [[nodiscard]] winrt::HaloDesktop::SourcesNavParams BuildSourcesNavigation(
            winrt::hstring const& id) const override;
        [[nodiscard]] std::optional<winrt::HaloDesktop::PlaybackRequest> OfflineNext(
            winrt::hstring const& id) const override;
        [[nodiscard]] std::optional<std::filesystem::path> SubtitlePath(
            winrt::hstring const& id) const override;
        [[nodiscard]] bool IsRunning() const noexcept override;
        [[nodiscard]] bool IsPausedAll() const noexcept override;
        [[nodiscard]] bool HasCompleted(winrt::hstring const& videoId) const noexcept override;
        [[nodiscard]] std::vector<Downloads::CompletedDownloadSource> CompletedFor(
            winrt::hstring const& videoId) const override;
        [[nodiscard]] std::vector<Downloads::VideoDownload> DownloadsFor(
            winrt::hstring const& videoId) const override;
        [[nodiscard]] std::int32_t ActiveCount() const noexcept override;
        [[nodiscard]] std::vector<Downloads::DownloadRecord> const& Records() const noexcept override;
        [[nodiscard]] std::vector<double> const& Throughput() const noexcept override;
        [[nodiscard]] std::optional<std::uint64_t> FreeBytes() const noexcept override;
        [[nodiscard]] std::filesystem::path DownloadDirectory() const override;
        [[nodiscard]] concurrency::task<void> SetDownloadDirectoryAsync(
            std::filesystem::path directory) override;
        [[nodiscard]] winrt::hstring ActionError() const override;
        DownloadChangedToken AddChangedHandler(DownloadChangedHandler handler) override;
        void RemoveChangedHandler(DownloadChangedToken token) noexcept override;

    private:
        struct Snapshot final
        {
            std::vector<Downloads::DownloadRecord> Records;
            std::optional<std::uint64_t> FreeBytes;
            std::filesystem::path Directory;
            // The engine event sequence when the list read began: events after
            // it may be missing from the list, and the records that applied
            // them win for their jobs.
            std::uint64_t Since{};
        };

        void RequestSynchronize();
        void ApplyEngineProgress(Downloads::DownloadRecord record, std::uint64_t accountVersion, std::uint64_t sequence);
        void ApplySnapshot(Snapshot snapshot, std::uint64_t version);
        void EnrichMissingArtwork();
        // Saves the logo and landscape art beside each finished download that
        // lacks them, so its card works offline. Each job and picture is tried
        // once until the network comes back (RetryMissingPictures); the work
        // runs off the UI thread and writes only through the engine.
        void EnrichMissingSidecars();
        // Windows reported a network change: once it settles with internet
        // access, RetryMissingPictures runs. UI thread.
        void OnNetworkStatusChanged();
        // The pictures that could not be had (offline at launch, say) are asked
        // for again: art for downloads still without any, and every missing
        // saved logo and art. A picture already saved is never fetched again.
        void RetryMissingPictures();
        static winrt::fire_and_forget SaveSidecarAsync(
            std::weak_ptr<DownloadService> weak,
            std::shared_ptr<Downloads::TransferEngine> engine,
            std::shared_ptr<DownloadSidecarFetcher> fetcher,
            std::wstring jobId,
            std::wstring accountKey,
            Downloads::SidecarKind kind,
            std::wstring url,
            std::filesystem::path target,
            std::uint64_t accountVersion);
        void RecordsChanged();
        [[nodiscard]] std::vector<DownloadsLogic::PausableRecord> PausableRecords() const;
        void SampleThroughput();
        void UpdateThroughputTimer();
        [[nodiscard]] bool HasActiveTransfer() const noexcept;
        [[nodiscard]] bool HasThroughputHistory() const;
        void RunEngineAction(std::function<void()> action);
        void NotifyChanged();
        [[nodiscard]] std::optional<Downloads::DownloadRecord> FindRecord(
            winrt::hstring const& id) const;
        [[nodiscard]] winrt::HaloDesktop::PlaybackRequest BuildPlaybackRequest(
            Downloads::DownloadRecord const& record) const;

        std::shared_ptr<Downloads::TransferEngine> m_engine;
        std::shared_ptr<ISessionService> m_session;
        // Transfers are the only sustained throughput this app produces, so they
        // are also the only honest measurement of the line behind it.
        std::shared_ptr<DevicePreferencesStore> m_devicePreferences;
        std::shared_ptr<DownloadArtworkService> m_artwork;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        // The chart is sampled on a clock rather than on progress events, so its
        // slots always mean seconds no matter how many transfers are reporting.
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_throughputTimer{ nullptr };
        winrt::event_token m_throughputTickToken{};
        // A network change starts this; it fires once the changes have been
        // quiet for a while, since Windows reports a reconnect as a burst.
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_networkRetryTimer{ nullptr };
        winrt::event_token m_networkRetryTickToken{};
        winrt::event_token m_networkStatusToken{};
        // MB/s, one sample per chart bar, oldest first; at most ThroughputSlots.
        std::vector<double> m_throughput;
        std::vector<Downloads::DownloadRecord> m_records;
        std::unordered_map<DownloadChangedToken, DownloadChangedHandler> m_handlers;
        DownloadsLogic::JobIdSet m_pauseAllIds;
        std::set<std::wstring, std::less<>> m_artworkPending;
        // Shared with the saves in flight, which may outlive a call into this.
        std::shared_ptr<DownloadSidecarFetcher> m_sidecarFetcher;
        // `<job id>|logo` and `<job id>|art` already tried this session. UI thread only.
        std::set<std::wstring, std::less<>> m_sidecarsTried;
        Downloads::DownloadChangedToken m_engineToken{};
        DownloadChangedToken m_nextToken{ 1 };
        std::atomic_uint64_t m_snapshotVersion{ 0 };
        std::atomic_uint64_t m_snapshotFloor{ 0 };
        std::atomic_uint64_t m_accountVersion{ 0 };
        std::atomic_uint64_t m_boundAccountVersion{ 0 };
        // Stamped on every engine event as it is raised, on the engine's thread.
        std::atomic_uint64_t m_eventSequence{ 0 };
        // The sequence of the last event each job's current record applied.
        // UI thread only; cleared with the account.
        std::unordered_map<std::wstring, std::uint64_t> m_lastEventSequence;
        std::mutex m_accountMutex;
        std::uint64_t m_appliedSnapshotVersion{};
        // The event sequence the last applied list read began at.
        std::uint64_t m_appliedSince{};
        std::optional<std::uint64_t> m_freeBytes;
        std::filesystem::path m_directory;
        bool m_running{};
        winrt::hstring m_actionError;
    };
}
