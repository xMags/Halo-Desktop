#include "pch.h"
#include "Services/WatchStateService.h"
#include "Api/BackendSelector.h"
#include "Api/HaloBackend.h"
#include "Services/WatchMarkPolicy.h"
#include "Services/WatchOutbox.h"
#include "Services/WatchOutboxPolicy.h"
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <utility>
#include <winrt/Windows.Networking.Connectivity.h>
namespace
{
    std::int64_t NowMilliseconds() noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // A connection that flaps raises its status many times a second; one send per
    // window is plenty, and a send already out covers what is staged.
    constexpr std::chrono::seconds NetworkSendThrottle{ 3 };
}
namespace HaloDesktop::Services
{
    struct WatchStateService::NetworkWatch final
    {
        winrt::Microsoft::UI::Dispatching::DispatcherQueue Dispatcher{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer Timer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker TickRevoker{};
        winrt::event_token StatusToken{};
    };

    WatchStateService::WatchStateService(
        std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
        std::shared_ptr<WatchOutbox> outbox)
        : m_backends(std::move(backends)), m_outbox(std::move(outbox))
    {
        if (!m_backends || !m_outbox) throw std::invalid_argument{ "WatchStateService requires a backend selector and an outbox." };
    }

    WatchStateService::~WatchStateService()
    {
        if (!m_network) return;
        try
        {
            if (m_network->StatusToken.value != 0)
            {
                winrt::Windows::Networking::Connectivity::NetworkInformation::NetworkStatusChanged(m_network->StatusToken);
            }
            if (m_network->Timer) m_network->Timer.Stop();
        }
        catch (...)
        {
        }
    }

    concurrency::task<void> WatchStateService::LoadAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        auto const writeVersion = m_writeVersion;
        auto const loadVersion = ++m_loadVersion;
        auto rows = co_await m_backends->Current()->GetWatchStateAsync();
        co_await uiContext;
        if (writeVersion == m_writeVersion && loadVersion == m_loadVersion)
        {
            CommitRows(std::move(rows));
        }
    }
    concurrency::task<void> WatchStateService::PutAsync(::HaloDesktop::Api::Dto::WatchEntry row)
    {
        std::vector<::HaloDesktop::Api::Dto::WatchEntry> rows;
        rows.push_back(std::move(row));
        co_await PutRowsAsync(std::move(rows));
    }
    // The server answers a write with every row the account has, so a write that
    // is still the newest one replaces the snapshot outright.
    concurrency::task<void> WatchStateService::PutRowsAsync(std::vector<::HaloDesktop::Api::Dto::WatchEntry> rows)
    {
        if (rows.empty())
        {
            co_return;
        }
        auto const uiContext = winrt::apartment_context{};
        // A run of marks is stamped past the one issued stamp it started from,
        // so the stamps it writes are what later ones must outrank.
        for (auto const& row : rows)
        {
            NoteTimestamp(row.UpdatedAt);
        }
        auto const version = ++m_writeVersion;
        auto stored = co_await m_backends->Current()->PutWatchStateAsync(std::move(rows));
        co_await uiContext;
        if (version == m_writeVersion)
        {
            CommitRows(std::move(stored));
        }
    }

    void WatchStateService::StageProgress(::HaloDesktop::Api::Dto::WatchEntry row)
    {
        NoteTimestamp(row.UpdatedAt);
        std::vector<::HaloDesktop::Api::Dto::WatchEntry> rows;
        rows.push_back(std::move(row));
        m_outbox->Stage(std::move(rows));
        // The last run's rows could not be read when this owner was bound, so
        // nothing reaches the disk until they are: try the read again, one at a
        // time. Progress is staged every couple of seconds while playing.
        if (m_outbox->AwaitingStored() && m_outboxReadEpoch != m_outbox->Epoch())
        {
            LoadOutboxAsync(m_outbox->Epoch(), m_outbox->AccountKey());
        }
    }

    concurrency::task<void> WatchStateService::ReportProgressAsync(::HaloDesktop::Api::Dto::WatchEntry row)
    {
        // Staged before anything is awaited, so even a report the window closes on
        // is on its way to disk.
        StageProgress(std::move(row));
        co_await SendPendingAsync();
    }

    concurrency::task<void> WatchStateService::SendPendingAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        // Read, filtered and sent under the owner they were staged for: a change of
        // account empties the outbox first and moves the epoch, so neither this
        // send's rows nor its settle can cross into the next account.
        auto const outboxEpoch = m_outbox->Epoch();
        auto const accountEpoch = m_accountEpoch;
        auto pending = m_outbox->Pending();
        if (pending.empty())
        {
            co_return;
        }
        if (m_loaded)
        {
            // The server keeps the newest row per video. A staged row it already
            // holds an equal or newer row for would change nothing there.
            auto split = SplitPendingWatchRows(pending, m_rows);
            m_outbox->Settle(outboxEpoch, split.AlreadyKnown);
            pending = std::move(split.ToSend);
            if (pending.empty())
            {
                co_return;
            }
        }
        for (auto const& row : pending)
        {
            NoteTimestamp(row.UpdatedAt);
        }
        auto const version = ++m_writeVersion;
        ++m_sendsInFlight;
        std::vector<::HaloDesktop::Api::Dto::WatchEntry> stored;
        std::exception_ptr failure;
        try
        {
            stored = co_await m_backends->Current()->PutWatchStateAsync(pending);
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        co_await uiContext;
        --m_sendsInFlight;
        if (failure)
        {
            // Everything stays staged for the next report, launch or reconnect.
            std::rethrow_exception(failure);
        }
        // Rows staged again while this was out carry newer times and stay.
        m_outbox->Settle(outboxEpoch, pending);
        if (version == m_writeVersion && accountEpoch == m_accountEpoch)
        {
            CommitRows(std::move(stored));
        }
    }

    concurrency::task<void> WatchStateService::FlushOutboxAsync() const
    {
        return m_outbox->FlushAsync();
    }

    concurrency::task<void> WatchStateService::RemoveItemAsync(winrt::hstring itemId)
    {
        co_await RemoveRowsAsync(HistoryRowsOf(m_rows, itemId));
    }

    concurrency::task<void> WatchStateService::RemoveVideosAsync(std::vector<winrt::hstring> videoIds)
    {
        co_await RemoveRowsAsync(HistoryRowsOfVideos(m_rows, videoIds));
    }

    concurrency::task<void> WatchStateService::RemoveRowsAsync(std::vector<::HaloDesktop::Api::Dto::WatchEntry> targets)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const epoch = m_accountEpoch;
        if (targets.empty())
        {
            co_return;
        }
        auto const tombstones = RemovedRows(targets, IssueTimestamp(NowMilliseconds()));
        for (auto const& row : tombstones)
        {
            NoteTimestamp(row.UpdatedAt);
        }

        std::exception_ptr failure;
        try
        {
            std::optional<std::vector<::HaloDesktop::Api::Dto::WatchEntry>> latest;
            for (auto& chunk : ChunkRows(tombstones, WatchRemovalChunkSize))
            {
                // Never written into the account signed in since this began.
                if (epoch != m_accountEpoch)
                {
                    co_return;
                }
                auto const version = ++m_writeVersion;
                auto stored = co_await m_backends->Current()->PutWatchStateAsync(std::move(chunk));
                co_await uiContext;
                if (epoch != m_accountEpoch)
                {
                    co_return;
                }
                if (version == m_writeVersion)
                {
                    CommitRows(stored);
                }
                latest = std::move(stored);
            }
            // A server that knows removals never sends a tombstoned video back;
            // one that does not stores the rows and answers with them, as live.
            if (latest && EchoesRemovedRows(*latest, tombstones))
            {
                throw HistoryRemovalUnsupported{};
            }
        }
        catch (...)
        {
            failure = std::current_exception();
        }

        if (epoch == m_accountEpoch)
        {
            try
            {
                co_await LoadAsync();
            }
            catch (...)
            {
                // The snapshot from the last accepted write stands.
            }
        }
        if (failure)
        {
            std::rethrow_exception(failure);
        }
    }

    std::int64_t WatchStateService::IssueTimestamp(std::int64_t now) noexcept
    {
        auto const stamp = m_lastTimestamp == (std::numeric_limits<std::int64_t>::max)()
            ? m_lastTimestamp
            : (std::max)(now, m_lastTimestamp + 1);
        m_lastTimestamp = stamp;
        return stamp;
    }

    void WatchStateService::NoteTimestamp(std::int64_t stamp) noexcept
    {
        m_lastTimestamp = (std::max)(m_lastTimestamp, stamp);
    }

    void WatchStateService::CommitRows(std::vector<::HaloDesktop::Api::Dto::WatchEntry> rows)
    {
        auto const changed = !m_loaded || !SameWatchRows(m_rows, rows);
        m_rows = std::move(rows);
        m_loaded = true;
        if (changed)
        {
            NotifyChanged();
        }
    }

    void WatchStateService::NotifyChanged()
    {
        // Copied first: a handler is free to unsubscribe from inside itself.
        std::vector<WatchStateChangedHandler> handlers;
        handlers.reserve(m_changedHandlers.size());
        for (auto const& entry : m_changedHandlers)
        {
            handlers.push_back(entry.second);
        }
        for (auto const& handler : handlers)
        {
            try
            {
                handler();
            }
            catch (...)
            {
            }
        }
    }

    WatchStateChangedToken WatchStateService::AddChangedHandler(WatchStateChangedHandler handler)
    {
        if (!handler)
        {
            return 0;
        }
        auto const token = m_nextChangedToken++;
        m_changedHandlers.emplace(token, std::move(handler));
        return token;
    }

    void WatchStateService::RemoveChangedHandler(WatchStateChangedToken token) noexcept
    {
        m_changedHandlers.erase(token);
    }

    std::vector<::HaloDesktop::Api::Dto::WatchEntry> WatchStateService::Rows() const { return LiveWatchRows(m_rows); }
    bool WatchStateService::HasLoaded() const noexcept { return m_loaded; }
    std::optional<::HaloDesktop::Api::Dto::WatchEntry> WatchStateService::Find(winrt::hstring const& videoId)const{auto found=std::find_if(m_rows.begin(),m_rows.end(),[&](auto const&row){return row.VideoId==videoId&&!row.RemovedAt;});return found==m_rows.end()?std::nullopt:std::optional<::HaloDesktop::Api::Dto::WatchEntry>{*found};}
    void WatchStateService::OnAccountChanged(std::wstring accountKey) noexcept
    {
        ++m_writeVersion;
        ++m_loadVersion;
        ++m_accountEpoch;
        m_rows.clear();
        m_loaded = false;
        try
        {
            // Bound before this returns: the identity handler runs before anything
            // can report or flush under the new account.
            if (m_outbox->Bind(accountKey))
            {
                LoadOutboxAsync(m_outbox->Epoch(), std::move(accountKey));
            }
        }
        catch (...)
        {
        }
    }

    winrt::fire_and_forget WatchStateService::LoadOutboxAsync(std::uint64_t epoch, std::wstring accountKey)
    {
        auto const weak = weak_from_this();
        auto const uiContext = winrt::apartment_context{};
        auto const outbox = m_outbox;
        m_outboxReadEpoch = epoch;
        std::optional<std::vector<::HaloDesktop::Api::Dto::WatchEntry>> stored;
        try
        {
            stored = co_await outbox->ReadStoredAsync(std::move(accountKey));
        }
        catch (...)
        {
        }
        co_await uiContext;
        auto const self = weak.lock();
        if (!self)
        {
            co_return;
        }
        // Only this read's own mark: a newer owner's read may be out already.
        if (self->m_outboxReadEpoch == epoch)
        {
            self->m_outboxReadEpoch.reset();
        }
        if (epoch != self->m_outbox->Epoch())
        {
            co_return;
        }
        // Not read this time: nothing is adopted, so the file and its rows stay
        // as they are, and the next staged progress tries again (StageProgress).
        if (!stored)
        {
            co_return;
        }
        // A row left by the last run was stamped then; whatever this run stamps
        // next (a removal above all) has to outrank it.
        for (auto const& row : *stored)
        {
            self->NoteTimestamp(row.UpdatedAt);
        }
        self->m_outbox->Adopt(epoch, std::move(*stored));
        // What the last run could not send goes out as the app starts.
        if (self->m_outbox->HasPending())
        {
            self->SendPendingAsync().then([](concurrency::task<void> sent)
            {
                try { sent.get(); } catch (...) {}
            });
        }
    }

    void WatchStateService::StartNetworkWatch(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher)
    {
        if (m_network || !dispatcher)
        {
            return;
        }
        auto watch = std::make_unique<NetworkWatch>();
        watch->Dispatcher = dispatcher;
        watch->Timer = dispatcher.CreateTimer();
        watch->Timer.Interval(NetworkSendThrottle);
        watch->Timer.IsRepeating(false);
        watch->TickRevoker = watch->Timer.Tick(winrt::auto_revoke, [weak = weak_from_this()](auto const&, auto const&)
        {
            if (auto const self = weak.lock())
            {
                // One send at a time: a send already out carries what is staged.
                if (self->m_sendsInFlight == 0 && self->m_outbox->HasPending())
                {
                    self->SendPendingAsync().then([](concurrency::task<void> sent)
                    {
                        try { sent.get(); } catch (...) {}
                    });
                }
            }
        });
        // The status callback arrives on a thread-pool thread. It only hands the
        // news to the UI thread; nothing it captures is touched there.
        watch->StatusToken = winrt::Windows::Networking::Connectivity::NetworkInformation::NetworkStatusChanged(
            [weak = weak_from_this(), dispatcher](winrt::Windows::Foundation::IInspectable const&)
            {
                try
                {
                    static_cast<void>(dispatcher.TryEnqueue([weak]()
                    {
                        if (auto const self = weak.lock())
                        {
                            self->OnNetworkStatusChanged();
                        }
                    }));
                }
                catch (...)
                {
                }
            });
        m_network = std::move(watch);
    }

    void WatchStateService::OnNetworkStatusChanged()
    {
        if (!m_network || !m_network->Timer || !m_outbox->HasPending())
        {
            return;
        }
        // A send is already scheduled for this burst of changes; later ones in the
        // same window ride on it.
        if (m_network->Timer.IsRunning())
        {
            return;
        }
        m_network->Timer.Start();
    }
}
