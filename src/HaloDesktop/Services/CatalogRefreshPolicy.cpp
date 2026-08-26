#include "Services/CatalogRefreshPolicy.h"

namespace HaloDesktop::Services
{
    CatalogCommitDecision DecideCatalogCommit(
        std::size_t eligibleCatalogCount,
        std::size_t successfulCatalogCount) noexcept
    {
        if (eligibleCatalogCount == 0 || successfulCatalogCount > 0)
        {
            return CatalogCommitDecision::Commit;
        }
        return CatalogCommitDecision::PreserveAndFail;
    }

    bool CatalogRefreshState::HasLoaded() const noexcept
    {
        return m_loaded;
    }

    bool CatalogRefreshState::IsDirty() const noexcept
    {
        return m_dirty;
    }

    bool CatalogRefreshState::RefreshInFlight() const noexcept
    {
        return m_refreshInFlight;
    }

    void CatalogRefreshState::Invalidate() noexcept
    {
        ++m_invalidationVersion;
        m_dirty = true;
    }

    std::optional<CatalogRefreshTicket> CatalogRefreshState::TryBeginRefresh() noexcept
    {
        if (m_refreshInFlight)
        {
            return std::nullopt;
        }
        m_refreshInFlight = true;
        return CatalogRefreshTicket{ m_invalidationVersion };
    }

    void CatalogRefreshState::CompleteRefresh(CatalogRefreshTicket ticket, bool committed) noexcept
    {
        m_refreshInFlight = false;
        if (!committed)
        {
            return;
        }

        m_loaded = true;
        m_dirty = ticket.InvalidationVersion != m_invalidationVersion;
    }
} // namespace HaloDesktop::Services
