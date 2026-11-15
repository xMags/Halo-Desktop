#include "pch.h"
#include "Services/WatchStateService.h"
#include "Api/ApiClient.h"
#include "Services/WatchMarkPolicy.h"
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <utility>
namespace
{
    std::int64_t NowMilliseconds() noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
}
namespace HaloDesktop::Services
{
    WatchStateService::WatchStateService(std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient) : m_apiClient(std::move(apiClient))
    {
        if (!m_apiClient) throw std::invalid_argument{ "WatchStateService requires an API client." };
    }
    concurrency::task<void> WatchStateService::LoadAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        auto const writeVersion = m_writeVersion;
        auto const loadVersion = ++m_loadVersion;
        auto rows = co_await m_apiClient->GetWatchStateAsync();
        co_await uiContext;
        if (writeVersion == m_writeVersion && loadVersion == m_loadVersion)
        {
            m_rows = std::move(rows);
            m_loaded = true;
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
        auto stored = co_await m_apiClient->PutWatchStateAsync(std::move(rows));
        co_await uiContext;
        if (version == m_writeVersion)
        {
            m_rows = std::move(stored);
            m_loaded = true;
        }
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
                auto stored = co_await m_apiClient->PutWatchStateAsync(std::move(chunk));
                co_await uiContext;
                if (epoch != m_accountEpoch)
                {
                    co_return;
                }
                if (version == m_writeVersion)
                {
                    m_rows = stored;
                    m_loaded = true;
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

    std::vector<::HaloDesktop::Api::Dto::WatchEntry> WatchStateService::Rows() const { return LiveWatchRows(m_rows); }
    bool WatchStateService::HasLoaded() const noexcept { return m_loaded; }
    std::optional<::HaloDesktop::Api::Dto::WatchEntry> WatchStateService::Find(winrt::hstring const& videoId)const{auto found=std::find_if(m_rows.begin(),m_rows.end(),[&](auto const&row){return row.VideoId==videoId&&!row.RemovedAt;});return found==m_rows.end()?std::nullopt:std::optional<::HaloDesktop::Api::Dto::WatchEntry>{*found};}
    void WatchStateService::OnAccountChanged() noexcept
    {
        ++m_writeVersion;
        ++m_loadVersion;
        ++m_accountEpoch;
        m_rows.clear();
        m_loaded = false;
    }
}
