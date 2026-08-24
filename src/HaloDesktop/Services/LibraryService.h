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
    private:
        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::vector<::HaloDesktop::Api::Dto::LibraryRow> m_rows;
    };
}
