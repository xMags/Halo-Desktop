#include "pch.h"
#include "Services/LibraryService.h"
#include "Api/ApiClient.h"
#include <stdexcept>
#include <utility>
namespace HaloDesktop::Services
{
    LibraryService::LibraryService(std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient) : m_apiClient(std::move(apiClient))
    {
        if (!m_apiClient) throw std::invalid_argument{ "LibraryService requires an API client." };
    }
    concurrency::task<void> LibraryService::LoadAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        auto rows = co_await m_apiClient->GetLibraryAsync();
        co_await uiContext;
        m_rows = std::move(rows);
    }
    std::vector<::HaloDesktop::Api::Dto::LibraryRow> LibraryService::Rows() const { return m_rows; }
}
