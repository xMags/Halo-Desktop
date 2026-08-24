#include "pch.h"
#include "Services/WatchStateService.h"
#include "Api/ApiClient.h"
#include <stdexcept>
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
        auto rows = co_await m_apiClient->GetWatchStateAsync();
        co_await uiContext;
        m_rows = std::move(rows);
    }
    std::vector<::HaloDesktop::Api::Dto::WatchEntry> WatchStateService::Rows() const { return m_rows; }
}
