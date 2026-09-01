#include "Services/Downloads/TransferRateWindow.h"

namespace HaloDesktop::Services::Downloads
{
    TransferRateWindow::TransferRateWindow(Clock::duration window)
        : m_window(window)
    {
    }

    void TransferRateWindow::Reset(Clock::time_point now, std::uint64_t bytes)
    {
        m_samples.clear();
        m_samples.emplace_back(now, bytes);
    }

    std::uint64_t TransferRateWindow::Record(Clock::time_point now, std::uint64_t bytes)
    {
        if (m_samples.empty())
        {
            m_samples.emplace_back(now, bytes);
            return 0;
        }
        if (bytes < m_samples.back().second || now < m_samples.back().first)
        {
            // The byte count went backwards or the clock did. Either way the
            // history no longer describes this transfer.
            Reset(now, bytes);
            return 0;
        }

        m_samples.emplace_back(now, bytes);
        // Drop the oldest samples while the next one still spans the full
        // window, so the average covers the window and not much more.
        while (m_samples.size() > 1 && now - m_samples[1].first >= m_window)
        {
            m_samples.pop_front();
        }

        auto const& oldest = m_samples.front();
        auto const seconds = std::chrono::duration<double>(now - oldest.first).count();
        if (seconds <= 0.0)
        {
            return 0;
        }
        return static_cast<std::uint64_t>(static_cast<double>(bytes - oldest.second) / seconds);
    }
} // namespace HaloDesktop::Services::Downloads
