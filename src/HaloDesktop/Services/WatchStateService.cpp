#include "pch.h"
#include "Services/WatchStateService.h"
#include "Api/ApiClient.h"
#include <stdexcept>
#include <algorithm>
#include <utility>
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
        auto const version = ++m_writeVersion;
        auto stored = co_await m_apiClient->PutWatchStateAsync(std::move(rows));
        co_await uiContext;
        if (version == m_writeVersion)
        {
            m_rows = std::move(stored);
            m_loaded = true;
        }
    }
    std::vector<::HaloDesktop::Api::Dto::WatchEntry> WatchStateService::Rows() const { return m_rows; }
    bool WatchStateService::HasLoaded() const noexcept { return m_loaded; }
    std::optional<::HaloDesktop::Api::Dto::WatchEntry> WatchStateService::Find(winrt::hstring const& videoId)const{auto found=std::find_if(m_rows.begin(),m_rows.end(),[&](auto const&row){return row.VideoId==videoId;});return found==m_rows.end()?std::nullopt:std::optional<::HaloDesktop::Api::Dto::WatchEntry>{*found};}
    void WatchStateService::OnAccountChanged() noexcept
    {
        ++m_writeVersion;
        ++m_loadVersion;
        m_rows.clear();
        m_loaded = false;
    }
}
