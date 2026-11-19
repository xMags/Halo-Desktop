#pragma once

#include "Services/Downloads/DownloadIndexStore.h"
#include "Services/Downloads/RequestVault.h"
#include "Services/Downloads/SegmentedTransfer.h"

#include <chrono>
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
#include <string_view>
#include <thread>
#include <vector>

namespace HaloDesktop::Services::Downloads
{
    using DownloadChangedToken = std::uint64_t;
    using DownloadChangedHandler = std::function<void(DownloadRecord const&)>;
    // Told, outside every engine and store lock, about a workaround the engine
    // had to apply at launch. The text names what happened, never a path,
    // URL or record content.
    using LaunchRecoveryReporter = std::function<void(std::wstring_view)>;

    // The engine's tunable waits and connection strategy. Production uses the
    // defaults; tests shorten them so nothing waits for real.
    struct TransferEngineOptions final
    {
        SegmentedOptions Transfer;
        // The longest a subtitle request may wait for its response.
        std::chrono::milliseconds SubtitleResponse{ 45000 };
    };

    // Thread-safe, single-worker FIFO transfer engine. Public calls may come
    // from any thread. Changed handlers run on the worker or calling thread and
    // are always invoked outside the engine lock. UI consumers must dispatch.
    // Destruction requests cancellation; a transfer notices it within the cancel
    // poll interval, including while it waits for a response. It leaves
    // non-paused active work recoverable.
    class TransferEngine final
    {
    public:
        explicit TransferEngine(
            std::filesystem::path dataRoot,
            LaunchRecoveryReporter const& launchRecovery = {},
            TransferEngineOptions options = {});
        ~TransferEngine();

        TransferEngine(TransferEngine const&) = delete;
        TransferEngine& operator=(TransferEngine const&) = delete;

        void SetAccount(std::wstring serverUrl, std::wstring userId);
        void ClearAccount();
        // Gives the downloads of partition `from` (the account keys MakeAccountKey
        // makes) to `to`, when someone signs in after using Halo without an
        // account (the reference's adopt_account). Files and request vault
        // entries stay where they are; only the owner in the index changes, in
        // one atomic rewrite, so transfers move as they were. Where both already
        // hold the same video, one copy stays (Downloads::PlanAdoption) and the
        // other is then removed like any download; a removal that fails is left
        // for the next launch and does not undo the adoption. Throws when the
        // index cannot be rewritten, which changes nothing. Blocking: call it
        // off the UI thread.
        void AdoptAccount(std::wstring const& from, std::wstring const& to);
        // Gives every other partition's downloads (OtherOwners) to the active
        // account, one AdoptAccount each. The downloads on this PC are its
        // viewer's: a changed server address or a reinstall gives the same
        // person a new account key, and without this the downloads saved under
        // the old key would be hidden while keeping their storage. Does nothing
        // without an active account. Throws, like AdoptAccount, when the index
        // cannot be rewritten; owners adopted before that stay adopted.
        // Blocking: call it off the UI thread.
        void AdoptOtherOwners();
        [[nodiscard]] std::vector<DownloadRecord> List();

        [[nodiscard]] DownloadRecord Start(DownloadStartRequest request);
        void Pause(std::wstring const& jobId);
        void Resume(std::wstring const& jobId);
        void Remove(std::wstring const& jobId);
        // Remove for a download that has not finished: decided under the same
        // removal gate the end of a run takes, so a download that finished
        // first is left alone (with its file) instead of deleted. Returns
        // false when it was left alone for that reason.
        bool Cancel(std::wstring const& jobId);
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

        enum class RemovalScope
        {
            AnyState,
            // Leaves a finished download alone.
            UnfinishedOnly,
        };

        // Remove, for a record of one of `owners`, or of the active account
        // when that is null. False when `scope` left a finished download alone.
        bool RemoveOwned(
            std::wstring const& jobId,
            std::vector<std::wstring> const* owners,
            RemovalScope scope = RemovalScope::AnyState);
        // Pause asked for by the viewer (`explicitPause`) or made because the
        // owner changed, which adoption may undo (AdoptAccount).
        void PauseFor(std::wstring const& jobId, bool explicitPause);
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
            DownloadRecord const& runRecord,
            std::optional<TransferResult> result,
            std::optional<TransferError> error);
        void FinishRemoved(
            std::wstring const& jobId,
            std::optional<DownloadRecord> current,
            DownloadRecord const& files);
        void UpdateResponseMetadata(
            std::wstring const& jobId,
            std::optional<std::wstring> validator,
            std::uint64_t totalBytes);
        void UpdateProgress(
            std::wstring const& jobId,
            SegmentedProgress const& progress);

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

        TransferEngineOptions m_options;
        DownloadIndexStore m_store;
        RequestVault m_vault;
        // Orders a removal against the end of a run, so the run either sees the
        // tombstone and cleans up itself or has fully finished before Remove
        // decides to clean up. Taken before m_mutex, never while holding it.
        std::mutex m_removalGate;
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
