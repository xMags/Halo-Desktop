#include "Services/Downloads/DownloadPageOperationState.h"

namespace HaloDesktop::Services::Downloads
{
    void DownloadPageOperationState::NavigatedTo() noexcept
    {
        ++m_pageGeneration;
    }

    void DownloadPageOperationState::Loaded() noexcept
    {
        m_loaded = true;
    }

    void DownloadPageOperationState::Unloaded() noexcept
    {
        m_loaded = false;
        ++m_pageGeneration;
    }

    std::optional<DownloadPageOperationState::Ticket> DownloadPageOperationState::TryBegin() noexcept
    {
        if (!m_loaded || m_inFlight)
        {
            return std::nullopt;
        }
        m_inFlight = true;
        return Ticket{
            .PageGeneration = m_pageGeneration,
            .OperationGeneration = ++m_operationGeneration,
        };
    }

    bool DownloadPageOperationState::CanApply(Ticket const& ticket) const noexcept
    {
        return m_loaded
            && m_inFlight
            && ticket.PageGeneration == m_pageGeneration
            && ticket.OperationGeneration == m_operationGeneration;
    }

    void DownloadPageOperationState::Complete(Ticket const& ticket) noexcept
    {
        if (ticket.OperationGeneration == m_operationGeneration)
        {
            m_inFlight = false;
        }
    }

    bool DownloadPageOperationState::InFlight() const noexcept
    {
        return m_inFlight;
    }
}
