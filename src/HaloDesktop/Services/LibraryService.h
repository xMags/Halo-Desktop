#pragma once

#include "Api/Dto.h"

#include <cstdint>
#include <memory>
#include <pplawait.h>
#include <ppltasks.h>
#include <optional>
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
        // True once the account's rows have arrived. Before that Contains answers
        // false for everything, which means "not known yet", not "not saved".
        [[nodiscard]] bool HasLoaded() const noexcept;
        [[nodiscard]] concurrency::task<bool> SetMembershipAsync(winrt::hstring type,winrt::hstring metaId,winrt::hstring name,std::optional<winrt::hstring> poster,bool present);
        void OnAccountChanged() noexcept;
    private:
        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::vector<::HaloDesktop::Api::Dto::LibraryRow> m_rows;
        std::optional<concurrency::task<void>> m_mutationTail;
        std::uint64_t m_requestVersion{};
        std::uint64_t m_accountVersion{};
        bool m_loaded{};
    };
}
