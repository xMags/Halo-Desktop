#pragma once

#include "Api/Dto.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <ppltasks.h>
#include <string>
#include <vector>

namespace HaloDesktop::Services
{
    // Watch progress that has not reached the server yet, kept in a small file
    // under the app's local data folder (the reference client's watch outbox).
    // The player stages its position here every couple of seconds and with every
    // report; a report sends everything staged and settles what the server took.
    //
    // The rows belong to one account on one server. Binding another account, or
    // signing out, clears them before anything else can read them, so a row
    // watched under one account can never be sent to another.
    //
    // Threading: every member except the file chain is used from one thread (the
    // UI thread in the app). File reads and writes run off that thread, strictly
    // one after another in the order they were asked for, and never overlap.
    class WatchOutbox final
    {
    public:
        // Reads and writes `watch-outbox.json` in `localState`.
        explicit WatchOutbox(std::filesystem::path localState);

        WatchOutbox(WatchOutbox const&) = delete;
        WatchOutbox& operator=(WatchOutbox const&) = delete;
        WatchOutbox(WatchOutbox&&) = delete;
        WatchOutbox& operator=(WatchOutbox&&) = delete;

        // Makes `accountKey` the owner, empty for nobody signed in. A change of
        // owner drops every row held for the last one at once and, when the last
        // owner was an account, removes them from disk. Returns true when the new
        // owner is an account whose stored rows have to be read (ReadStoredAsync).
        [[nodiscard]] bool Bind(std::wstring accountKey);

        // The rows stored for `accountKey`, read after every write already asked
        // for. Empty for a missing, foreign, corrupt or oversized file; nothing
        // (std::nullopt) when the file could not be read this time, which leaves
        // it as it is for another try. Never throws.
        [[nodiscard]] concurrency::task<std::optional<std::vector<Api::Dto::WatchEntry>>> ReadStoredAsync(std::wstring accountKey) const;

        // Merges rows read from disk into what was staged meanwhile, newest per
        // video, when `epoch` is still the current owner's; then writes the result,
        // which also replaces a file that could not be used.
        void Adopt(std::uint64_t epoch, std::vector<Api::Dto::WatchEntry> stored);

        // True from binding an account until its stored rows are adopted. Nothing
        // is written to the file meanwhile (a write would lose rows not read yet);
        // staged rows wait in memory, and the caller tries the read again.
        [[nodiscard]] bool AwaitingStored() const noexcept;

        // Keeps these rows until they are settled. Ignored while nobody is signed in.
        void Stage(std::vector<Api::Dto::WatchEntry> rows);

        // Drops the rows `sent` covered, unless one was staged again since with a
        // newer time. Ignored when the owner changed after the send began.
        void Settle(std::uint64_t epoch, std::vector<Api::Dto::WatchEntry> const& sent);

        [[nodiscard]] std::vector<Api::Dto::WatchEntry> Pending() const;
        [[nodiscard]] bool HasPending() const noexcept;
        // Bumped by every change of owner.
        [[nodiscard]] std::uint64_t Epoch() const noexcept;
        [[nodiscard]] std::wstring const& AccountKey() const noexcept;

        // Completes once every write asked for so far has reached the disk (or
        // failed; a failed write leaves the rows in memory for the next one).
        [[nodiscard]] concurrency::task<void> FlushAsync() const;

    private:
        struct FileChain;

        void Persist();
        // Queues writing `content`, or removing the file when there is none.
        void Enqueue(std::optional<std::string> content);

        std::shared_ptr<FileChain> m_files;
        std::vector<Api::Dto::WatchEntry> m_pending;
        std::wstring m_accountKey;
        std::uint64_t m_epoch{};
        bool m_bound{};
        bool m_awaitingStored{};
    };
}
