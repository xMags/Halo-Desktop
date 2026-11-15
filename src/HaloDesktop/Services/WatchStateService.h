#pragma once

#include "Api/Dto.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <stdexcept>
#include <vector>
namespace HaloDesktop::Api { class ApiClient; }
namespace HaloDesktop::Services
{
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

    // UI-thread-only watch-state snapshot repository.
    class WatchStateService final
    {
    public:
        explicit WatchStateService(std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient);
        [[nodiscard]] concurrency::task<void> LoadAsync();
        [[nodiscard]] concurrency::task<void> PutAsync(::HaloDesktop::Api::Dto::WatchEntry row);
        // Several rows in one request, so a run of marks lands together and in the
        // order its timestamps give it, or not at all.
        [[nodiscard]] concurrency::task<void> PutRowsAsync(std::vector<::HaloDesktop::Api::Dto::WatchEntry> rows);
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
        void OnAccountChanged() noexcept;
    private:
        void NoteTimestamp(std::int64_t stamp) noexcept;
        // Tombstones `targets` (rows read from the snapshot by the caller, on this
        // thread, before anything was awaited) in batches, then reads again.
        [[nodiscard]] concurrency::task<void> RemoveRowsAsync(std::vector<::HaloDesktop::Api::Dto::WatchEntry> targets);

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::vector<::HaloDesktop::Api::Dto::WatchEntry> m_rows;
        std::uint64_t m_writeVersion{};
        std::uint64_t m_loadVersion{};
        std::uint64_t m_accountEpoch{};
        // Deliberately kept across account changes: stamps only ever move forward.
        std::int64_t m_lastTimestamp{};
        bool m_loaded{};
    };
}
