#pragma once

#include "Api/Dto.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <vector>
namespace HaloDesktop::Api { class ApiClient; }
namespace HaloDesktop::Services
{
    // UI-thread-only watch-state snapshot repository.
    class WatchStateService final
    {
    public:
        explicit WatchStateService(std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient);
        [[nodiscard]] concurrency::task<void> LoadAsync();
        [[nodiscard]] concurrency::task<void> PutAsync(::HaloDesktop::Api::Dto::WatchEntry row);
        [[nodiscard]] std::vector<::HaloDesktop::Api::Dto::WatchEntry> Rows() const;
        [[nodiscard]] std::optional<::HaloDesktop::Api::Dto::WatchEntry> Find(winrt::hstring const& videoId) const;
    private:
        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::vector<::HaloDesktop::Api::Dto::WatchEntry> m_rows;
        std::uint64_t m_writeVersion{};
    };
}
