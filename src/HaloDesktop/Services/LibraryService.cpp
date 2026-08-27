#include "pch.h"
#include "Services/LibraryService.h"
#include "Api/ApiClient.h"
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <chrono>
namespace HaloDesktop::Services
{
    LibraryService::LibraryService(std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient) : m_apiClient(std::move(apiClient))
    {
        if (!m_apiClient) throw std::invalid_argument{ "LibraryService requires an API client." };
    }
    concurrency::task<void> LibraryService::LoadAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_requestVersion;
        auto rows = co_await m_apiClient->GetLibraryAsync();
        co_await uiContext;
        if (version == m_requestVersion)
        {
            m_rows = std::move(rows);
        }
    }
    std::vector<::HaloDesktop::Api::Dto::LibraryRow> LibraryService::Rows() const { return m_rows; }
    bool LibraryService::Contains(winrt::hstring type,winrt::hstring metaId)const{auto id=type+L":"+metaId;return std::any_of(m_rows.begin(),m_rows.end(),[&](auto const&r){return r.Id==id&&!r.RemovedAt;});}
    concurrency::task<void> LibraryService::SetMembershipAsync(
        winrt::hstring type,
        winrt::hstring metaId,
        winrt::hstring name,
        std::optional<winrt::hstring> poster,
        bool present)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_requestVersion;
        auto const id = type + L":" + metaId;
        auto const found = std::find_if(m_rows.begin(), m_rows.end(), [&id](auto const& row)
        {
            return row.Id == id;
        });
        auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::vector<::HaloDesktop::Api::Dto::LibraryRow> changed;
        if (present)
        {
            changed.push_back({
                id,
                type,
                name,
                poster,
                found == m_rows.end() ? now : found->AddedAt,
                std::nullopt,
                now,
            });
        }
        else if (found != m_rows.end())
        {
            auto row = *found;
            row.RemovedAt = now;
            row.UpdatedAt = now;
            changed.push_back(std::move(row));
        }
        if (changed.empty())
        {
            co_return;
        }
        auto rows = co_await m_apiClient->PutLibraryAsync(std::move(changed));
        co_await uiContext;
        if (version == m_requestVersion)
        {
            m_rows = std::move(rows);
        }
    }

    void LibraryService::OnAccountChanged() noexcept
    {
        ++m_requestVersion;
        m_rows.clear();
    }
}
