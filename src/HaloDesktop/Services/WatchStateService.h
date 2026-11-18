#pragma once

#include "Api/Dto.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
namespace winrt::Microsoft::UI::Dispatching { struct DispatcherQueue; }
namespace HaloDesktop::Api { class BackendSelector; }
namespace HaloDesktop::Services
{
    class WatchOutbox;

    // Thrown by a history removal when the server stored the removed rows and sent
    // them back: it is too old to know removals. Nothing was lost.
    class HistoryRemovalUnsupported final : public std::runtime_error
    {
    public:
        HistoryRemovalUnsupported()
            : std::runtime_error{ "Your Halo server cannot remove history yet. Nothing was lost; it needs updating first." }
        {
        }
    };

    using WatchStateChangedToken = std::uint64_t;
    using WatchStateChangedHandler = std::function<void()>;

    // UI-thread-only watch-state snapshot repository.
    class WatchStateService final : public std::enable_shared_from_this<WatchStateService>
    {
    public:
        WatchStateService(
            std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
            std::shared_ptr<WatchOutbox> outbox);
        ~WatchStateService();
        WatchStateService(WatchStateService const&) = delete;
        WatchStateService& operator=(WatchStateService const&) = delete;

        [[nodiscard]] concurrency::task<void> LoadAsync();
        [[nodiscard]] concurrency::task<void> PutAsync(::HaloDesktop::Api::Dto::WatchEntry row);
        // Several rows in one request, so a run of marks lands together and in the
        // order its timestamps give it, or not at all.
        [[nodiscard]] concurrency::task<void> PutRowsAsync(std::vector<::HaloDesktop::Api::Dto::WatchEntry> rows);
        // The player's progress, kept on this PC between reports so that closing
        // the window mid-playback costs a couple of seconds at most.
        void StageProgress(::HaloDesktop::Api::Dto::WatchEntry row);
        // A player report through the outbox: the row is staged first, then every
        // staged row is sent and what the server took is settled. A send that
        // fails keeps everything for the next report, the next launch or the
        // network coming back.
        [[nodiscard]] concurrency::task<void> ReportProgressAsync(::HaloDesktop::Api::Dto::WatchEntry row);
        // Sends what is staged, if anything; the launch and the network returning
        // call this.
        [[nodiscard]] concurrency::task<void> SendPendingAsync();
        // Completes once the staged rows are on disk, for the window's final report.
        [[nodiscard]] concurrency::task<void> FlushOutboxAsync() const;
        // Forgets every video of `itemId` in the watch history by writing
        // tombstones, in batches, then reads the history again whatever happened,
        // so a batch that failed partway leaves the snapshot showing what was
        // actually removed. Throws HistoryRemovalUnsupported when the server sends
        // the removed rows back, and the request's own error when a write fails.
        // A removal that outlives the account it began under stops writing and
        // returns quietly.
        [[nodiscard]] concurrency::task<void> RemoveItemAsync(winrt::hstring itemId);
        // The same removal for exactly these videos: the live rows of them when
        // this is called, and nothing else, so a video removed meanwhile is not
        // written again and one played meanwhile under another id is untouched.
        [[nodiscard]] concurrency::task<void> RemoveVideosAsync(std::vector<winrt::hstring> videoIds);
        // Both leave removal tombstones out: a removed video reads as never
        // played, whatever a server sends back.
        [[nodiscard]] std::vector<::HaloDesktop::Api::Dto::WatchEntry> Rows() const;
        [[nodiscard]] std::optional<::HaloDesktop::Api::Dto::WatchEntry> Find(winrt::hstring const& videoId) const;
        // True once the account's rows have arrived. Before that an empty Rows()
        // means "not known yet" rather than "nothing watched".
        [[nodiscard]] bool HasLoaded() const noexcept;
        // A write stamp strictly later than every stamp this service has issued
        // or written, and never earlier than `now`. The player's progress reports,
        // the watched marks and the history removal draw from it, so a report or
        // a mark made after a removal
        // always outranks the tombstone and a removal always outranks every
        // report made before it, whatever order the requests arrive in.
        [[nodiscard]] std::int64_t IssueTimestamp(std::int64_t now) noexcept;
        // `accountKey` names the account and server now signed in, empty for
        // nobody; the outbox is bound to it before anything can be sent.
        void OnAccountChanged(std::wstring accountKey) noexcept;
        // Sends the staged rows when the network comes back. The status callback
        // arrives on any thread; it is handed to `dispatcher` and throttled, so a
        // flapping connection cannot send more than once per few seconds.
        void StartNetworkWatch(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher);

        // Raised on this thread whenever the account's rows change: a read that
        // brought something new, a mark, a removal, or a player report the server
        // took. Staging alone never raises it, so the player's two-second staging
        // does not redraw anything.
        [[nodiscard]] WatchStateChangedToken AddChangedHandler(WatchStateChangedHandler handler);
        void RemoveChangedHandler(WatchStateChangedToken token) noexcept;

    private:
        void NoteTimestamp(std::int64_t stamp) noexcept;
        // Adopts a snapshot the server sent and tells subscribers when it differs.
        void CommitRows(std::vector<::HaloDesktop::Api::Dto::WatchEntry> rows);
        void NotifyChanged();
        winrt::fire_and_forget LoadOutboxAsync(std::uint64_t epoch, std::wstring accountKey);
        void OnNetworkStatusChanged();
        // Tombstones `targets` (rows read from the snapshot by the caller, on this
        // thread, before anything was awaited) in batches, then reads again.
        [[nodiscard]] concurrency::task<void> RemoveRowsAsync(std::vector<::HaloDesktop::Api::Dto::WatchEntry> targets);

        std::shared_ptr<::HaloDesktop::Api::BackendSelector> m_backends;
        std::shared_ptr<WatchOutbox> m_outbox;
        std::vector<::HaloDesktop::Api::Dto::WatchEntry> m_rows;
        std::unordered_map<WatchStateChangedToken, WatchStateChangedHandler> m_changedHandlers;
        WatchStateChangedToken m_nextChangedToken{ 1 };
        // The network subscription and its throttle timer, kept out of this header
        // so it stays free of the windowing projection.
        struct NetworkWatch;
        std::unique_ptr<NetworkWatch> m_network;
        std::uint64_t m_writeVersion{};
        std::uint64_t m_loadVersion{};
        std::uint64_t m_accountEpoch{};
        std::uint32_t m_sendsInFlight{};
        // Deliberately kept across account changes: stamps only ever move forward.
        std::int64_t m_lastTimestamp{};
        bool m_loaded{};
    };
}
