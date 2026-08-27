#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace HaloDesktop::Services
{
    enum class CatalogCommitDecision
    {
        Commit,
        PreserveAndFail,
    };

    [[nodiscard]] CatalogCommitDecision DecideCatalogCommit(
        std::size_t eligibleCatalogCount,
        std::size_t successfulCatalogCount) noexcept;

    struct CatalogRefreshTicket final
    {
        std::uint64_t InvalidationVersion{};
    };

    // Tracks refresh validity independently from the task that performs the work.
    // If addons change while a request is in flight, completing that older request
    // cannot clear the newer invalidation.
    class CatalogRefreshState final
    {
    public:
        [[nodiscard]] bool HasLoaded() const noexcept;
        [[nodiscard]] bool IsDirty() const noexcept;
        [[nodiscard]] bool RefreshInFlight() const noexcept;
        void Reset() noexcept;
        void Invalidate() noexcept;
        [[nodiscard]] std::optional<CatalogRefreshTicket> TryBeginRefresh() noexcept;
        void CompleteRefresh(CatalogRefreshTicket ticket, bool committed) noexcept;

    private:
        std::uint64_t m_invalidationVersion{};
        bool m_loaded{};
        bool m_dirty{ true };
        bool m_refreshInFlight{};
    };
} // namespace HaloDesktop::Services
