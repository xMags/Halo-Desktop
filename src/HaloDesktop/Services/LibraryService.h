#pragma once

#include "Api/Dto.h"

#include <memory>
#include <pplawait.h>
#include <ppltasks.h>
#include <vector>

namespace HaloDesktop::Api { class ApiClient; }
namespace HaloDesktop::Services
{
    // UI-thread-only library snapshot repository.
    class LibraryService final
    {
    public:
        explicit LibraryService(std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient);
        [[nodiscard]] concurrency::task<void> LoadAsync();
        [[nodiscard]] std::vector<::HaloDesktop::Api::Dto::LibraryRow> Rows() const;
        [[nodiscard]] bool Contains(winrt::hstring type,winrt::hstring metaId) const;
        [[nodiscard]] concurrency::task<void> SetMembershipAsync(winrt::hstring type,winrt::hstring metaId,winrt::hstring name,std::optional<winrt::hstring> poster,bool present);
    private:
        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::vector<::HaloDesktop::Api::Dto::LibraryRow> m_rows;
    };
}
