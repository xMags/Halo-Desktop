#pragma once

#include "Services/Downloads/TransferEngine.h"
#include "Services/ServiceInterfaces.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace HaloDesktop::Services
{
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
        [[nodiscard]] concurrency::task<DownloadStartOutcome> StartDownloadAsync(
            Downloads::DownloadStartRequest request) override;
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
        [[nodiscard]] std::int32_t ActiveCount() const noexcept override;
        [[nodiscard]] double AggregateRate() const noexcept override;
        [[nodiscard]] winrt::hstring QueueLine() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> Transfers() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> Ready() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<double> Throughput() const override;
        [[nodiscard]] std::uint64_t StoredBytes() const noexcept override;
        [[nodiscard]] std::uint64_t InFlightBytes() const noexcept override;
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
        };

        void RequestSynchronize();
        void ApplyEngineProgress(Downloads::DownloadRecord record, std::uint64_t accountVersion);
        void ApplySnapshot(Snapshot snapshot, std::uint64_t version);
        void RebuildObservables();
        void RunEngineAction(std::function<void()> action);
        void NotifyChanged();
        [[nodiscard]] std::optional<Downloads::DownloadRecord> FindRecord(
            winrt::hstring const& id) const;
        [[nodiscard]] winrt::HaloDesktop::PlaybackRequest BuildPlaybackRequest(
            Downloads::DownloadRecord const& record) const;

        std::shared_ptr<Downloads::TransferEngine> m_engine;
        std::shared_ptr<ISessionService> m_session;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> m_transfers{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> m_ready{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<double> m_throughput{ nullptr };
        std::vector<Downloads::DownloadRecord> m_records;
        std::unordered_map<DownloadChangedToken, DownloadChangedHandler> m_handlers;
        std::set<std::wstring, std::less<>> m_pauseAllIds;
        Downloads::DownloadChangedToken m_engineToken{};
        DownloadChangedToken m_nextToken{ 1 };
        std::atomic_uint64_t m_snapshotVersion{ 0 };
        std::atomic_uint64_t m_snapshotFloor{ 0 };
        std::atomic_uint64_t m_accountVersion{ 0 };
        std::atomic_uint64_t m_boundAccountVersion{ 0 };
        std::mutex m_accountMutex;
        std::uint64_t m_appliedSnapshotVersion{};
        std::uint64_t m_storedBytes{};
        std::uint64_t m_inFlightBytes{};
        std::optional<std::uint64_t> m_freeBytes;
        std::filesystem::path m_directory;
        double m_aggregateRate{};
        bool m_running{};
        bool m_pausedAll{};
        winrt::hstring m_actionError;
    };
}
