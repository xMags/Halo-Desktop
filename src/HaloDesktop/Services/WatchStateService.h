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
        // Several rows in one request, so a run of marks lands together and in the
        // order its timestamps give it, or not at all.
        [[nodiscard]] concurrency::task<void> PutRowsAsync(std::vector<::HaloDesktop::Api::Dto::WatchEntry> rows);
        [[nodiscard]] std::vector<::HaloDesktop::Api::Dto::WatchEntry> Rows() const;
        [[nodiscard]] std::optional<::HaloDesktop::Api::Dto::WatchEntry> Find(winrt::hstring const& videoId) const;
        // True once the account's rows have arrived. Before that an empty Rows()
        // means "not known yet" rather than "nothing watched".
        [[nodiscard]] bool HasLoaded() const noexcept;
        void OnAccountChanged() noexcept;
    private:
        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::vector<::HaloDesktop::Api::Dto::WatchEntry> m_rows;
        std::uint64_t m_writeVersion{};
        std::uint64_t m_loadVersion{};
        bool m_loaded{};
    };
}
