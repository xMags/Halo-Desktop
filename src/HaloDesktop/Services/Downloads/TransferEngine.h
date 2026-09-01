#pragma once

#include "Services/Downloads/DownloadIndexStore.h"
#include "Services/Downloads/RequestVault.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace HaloDesktop::Services::Downloads
{
    using DownloadChangedToken = std::uint64_t;
    using DownloadChangedHandler = std::function<void(DownloadRecord const&)>;

    // Thread-safe, single-worker FIFO transfer engine. Public calls may come
    // from any thread. Changed handlers run on the worker or calling thread and
    // are always invoked outside the engine lock. UI consumers must dispatch.
    // Destruction requests cancellation and waits at most for the active
    // WinHTTP read timeout. It leaves non-paused active work recoverable.
    class TransferEngine final
    {
    public:
        explicit TransferEngine(std::filesystem::path dataRoot);
        ~TransferEngine();

        TransferEngine(TransferEngine const&) = delete;
        TransferEngine& operator=(TransferEngine const&) = delete;

        void SetAccount(std::wstring serverUrl, std::wstring userId);
        void ClearAccount();
        [[nodiscard]] std::vector<DownloadRecord> List();

        [[nodiscard]] DownloadRecord Start(DownloadStartRequest request);
        void Pause(std::wstring const& jobId);
        void Resume(std::wstring const& jobId);
        void Remove(std::wstring const& jobId);
        bool SetLandscapeArtwork(
            std::wstring const& jobId,
            std::wstring const& expectedAccount,
            std::wstring artwork);

        [[nodiscard]] PlaybackFiles FilesForPlayback(std::wstring const& jobId);
        [[nodiscard]] std::filesystem::path DownloadDirectory() const;
        void SetDownloadDirectory(std::filesystem::path directory);
        [[nodiscard]] std::optional<std::uint64_t> FreeBytes() const noexcept;

        [[nodiscard]] DownloadChangedToken AddChangedHandler(DownloadChangedHandler handler);
        void RemoveChangedHandler(DownloadChangedToken token) noexcept;

    private:
        struct TransferResult;
        struct TransferError;

        void Worker(std::stop_token stopToken);
        [[nodiscard]] TransferResult TransferWithRetries(
            std::wstring const& jobId,
            std::shared_ptr<std::atomic_bool> const& cancel,
            std::stop_token stopToken);
        [[nodiscard]] TransferResult TransferOnce(
            std::wstring const& jobId,
            ProtectedRequest const& request,
            std::shared_ptr<std::atomic_bool> const& cancel,
            std::stop_token stopToken);
        [[nodiscard]] std::optional<std::pair<std::wstring, std::wstring>> DownloadSubtitle(
            SubtitleRequest const& request,
            std::filesystem::path const& target,
            std::shared_ptr<std::atomic_bool> const& cancel,
            std::stop_token stopToken) const noexcept;

        void Finish(
            std::wstring const& jobId,
            std::optional<TransferResult> result,
            std::optional<TransferError> error);
        void UpdateResponseMetadata(
            std::wstring const& jobId,
            std::optional<std::wstring> validator,
            std::uint64_t totalBytes);
        void UpdateProgress(
            std::wstring const& jobId,
            std::uint64_t downloadedBytes,
            std::uint64_t totalBytes,
            std::uint64_t bytesPerSecond,
            bool persist);

        [[nodiscard]] std::vector<DownloadRecord> SnapshotLocked() const;
        [[nodiscard]] std::vector<DownloadChangedHandler> HandlersLocked() const;
        [[nodiscard]] std::optional<DownloadRecord> VisibleRecordForVideoLocked(
            std::wstring const& videoId) const;
        [[nodiscard]] bool IsHiddenBackupLocked(std::wstring const& jobId) const;
        void Persist(std::vector<DownloadRecord> records, std::uint64_t generation);
        static void Notify(
            std::vector<DownloadChangedHandler> const& handlers,
            DownloadRecord const& record) noexcept;
        void ReconcileMissingFiles();

        DownloadIndexStore m_store;
        RequestVault m_vault;
        mutable std::mutex m_mutex;
        std::condition_variable_any m_condition;
        std::map<std::wstring, DownloadRecord, std::less<>> m_records;
        std::deque<std::wstring> m_queue;
        std::set<std::wstring, std::less<>> m_pendingVideoIds;
        std::optional<std::wstring> m_activeAccount;
        std::optional<std::wstring> m_activeJob;
        std::map<std::wstring, std::shared_ptr<std::atomic_bool>, std::less<>> m_cancel;
        std::map<DownloadChangedToken, DownloadChangedHandler> m_handlers;
        DownloadChangedToken m_nextHandlerToken{ 1 };
        std::uint64_t m_generation{ 1 };
        std::jthread m_worker;
    };

    // Unit-style invariant gate used by the M22a debug harness. It performs no
    // network access and throws if a security or resume invariant regresses.
    void RunDownloadEngineUnitChecks();
}
