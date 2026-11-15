#include "pch.h"
#include "Services/WatchOutbox.h"

#include "Services/WatchOutboxPolicy.h"
#include "Storage/FileStorage.h"

#include <mutex>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace
{
    constexpr wchar_t OutboxFileName[] = L"watch-outbox.json";
}

namespace HaloDesktop::Services
{
    // The file operations, chained so each starts only once the one before it has
    // finished, off the calling thread. A link never fails the chain: errors are
    // swallowed where they happen, and the rows stay in memory for the next write.
    // Shared with the links themselves, so a write still queued when the outbox
    // goes away completes against state that is still alive.
    struct WatchOutbox::FileChain final
    {
        explicit FileChain(std::filesystem::path path) : Path(std::move(path))
        {
        }

        std::filesystem::path const Path;
        std::mutex Mutex;
        concurrency::task<void> Tail{ concurrency::task_from_result() };

        // Content to write, or nothing to remove the file: an empty outbox leaves
        // no file behind, as the reference removes its key.
        void Write(std::optional<std::string> const& content) const noexcept
        {
            try
            {
                Storage::FileMutationLock const lock{ Path };
                if (content)
                {
                    Storage::WriteUtf8FileAtomic(Path, *content);
                    return;
                }
                std::error_code ignored;
                std::filesystem::remove(Path, ignored);
            }
            catch (...)
            {
            }
        }

        [[nodiscard]] std::vector<Api::Dto::WatchEntry> Read(std::wstring const& accountKey) const noexcept
        {
            try
            {
                Storage::FileMutationLock const lock{ Path };
                // An oversized file throws here and reads as nothing staged.
                return ParseWatchOutbox(Storage::ReadUtf8File(Path, WatchOutboxMaximumBytes), accountKey);
            }
            catch (...)
            {
                return {};
            }
        }
    };

    WatchOutbox::WatchOutbox(std::filesystem::path localState)
    {
        if (localState.empty())
        {
            throw std::invalid_argument{ "WatchOutbox requires a local state path." };
        }
        m_files = std::make_shared<FileChain>(std::filesystem::absolute(localState).lexically_normal() / OutboxFileName);
    }

    bool WatchOutbox::Bind(std::wstring accountKey)
    {
        if (m_bound && accountKey == m_accountKey)
        {
            return false;
        }
        auto const leavingAccount = m_bound && !m_accountKey.empty();
        // Dropped here, synchronously, before anything that follows the change of
        // owner can ask for pending rows: none of them can reach the new owner.
        ++m_epoch;
        m_pending.clear();
        m_accountKey = std::move(accountKey);
        m_bound = true;
        // Unsent progress belongs to the account it was watched under. The first
        // binding of a launch removes nothing: the file is the last run's rows,
        // read below and kept only if they belong to the owner bound now.
        if (leavingAccount)
        {
            Persist();
        }
        return !m_accountKey.empty();
    }

    concurrency::task<std::vector<Api::Dto::WatchEntry>> WatchOutbox::ReadStoredAsync(std::wstring accountKey) const
    {
        auto const files = m_files;
        concurrency::task<std::vector<Api::Dto::WatchEntry>> read;
        {
            std::scoped_lock const lock{ files->Mutex };
            read = files->Tail.then(
                [files, accountKey = std::move(accountKey)]()
                {
                    return files->Read(accountKey);
                });
            files->Tail = read.then(
                [](concurrency::task<std::vector<Api::Dto::WatchEntry>> completed)
                {
                    try { static_cast<void>(completed.get()); } catch (...) {}
                });
        }
        return read;
    }

    void WatchOutbox::Adopt(std::uint64_t epoch, std::vector<Api::Dto::WatchEntry> stored)
    {
        if (epoch != m_epoch || m_accountKey.empty())
        {
            return;
        }
        // What was staged since the read began is newer for its video, and the
        // merge keeps the newer row either way.
        m_pending = MergePendingWatchRows(stored, m_pending);
        Persist();
    }

    void WatchOutbox::Stage(std::vector<Api::Dto::WatchEntry> rows)
    {
        if (rows.empty() || !m_bound || m_accountKey.empty())
        {
            return;
        }
        m_pending = MergePendingWatchRows(m_pending, rows);
        Persist();
    }

    void WatchOutbox::Settle(std::uint64_t epoch, std::vector<Api::Dto::WatchEntry> const& sent)
    {
        if (epoch != m_epoch || sent.empty())
        {
            return;
        }
        auto remaining = WithoutSettledWatchRows(m_pending, sent);
        if (remaining.size() == m_pending.size())
        {
            return;
        }
        m_pending = std::move(remaining);
        Persist();
    }

    std::vector<Api::Dto::WatchEntry> WatchOutbox::Pending() const { return m_pending; }
    bool WatchOutbox::HasPending() const noexcept { return !m_pending.empty(); }
    std::uint64_t WatchOutbox::Epoch() const noexcept { return m_epoch; }
    std::wstring const& WatchOutbox::AccountKey() const noexcept { return m_accountKey; }

    concurrency::task<void> WatchOutbox::FlushAsync() const
    {
        std::scoped_lock const lock{ m_files->Mutex };
        return m_files->Tail;
    }

    void WatchOutbox::Persist()
    {
        // Serialized here, on the owning thread, so the write carries exactly the
        // state of this moment whatever happens to the rows before it runs.
        std::optional<std::string> content;
        if (!m_pending.empty() && !m_accountKey.empty())
        {
            content = SerializeWatchOutbox(m_accountKey, m_pending);
        }
        auto const files = m_files;
        std::scoped_lock const lock{ files->Mutex };
        files->Tail = files->Tail.then(
            [files, content = std::move(content)]()
            {
                files->Write(content);
            });
    }
}
