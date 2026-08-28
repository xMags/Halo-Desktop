#pragma once

#include <utility>

namespace HaloDesktop::Playback
{
    // Marks a synchronous callback as active for one stack frame. Nested entry
    // observes the existing marker and leaves it owned by the outer frame.
    class ScopedReentrancyGuard final
    {
    public:
        explicit ScopedReentrancyGuard(bool& active) noexcept
            : m_active(active), m_entered(!std::exchange(active, true))
        {
        }

        ~ScopedReentrancyGuard()
        {
            if (m_entered)
            {
                m_active = false;
            }
        }

        ScopedReentrancyGuard(ScopedReentrancyGuard const&) = delete;
        ScopedReentrancyGuard& operator=(ScopedReentrancyGuard const&) = delete;
        ScopedReentrancyGuard(ScopedReentrancyGuard&&) = delete;
        ScopedReentrancyGuard& operator=(ScopedReentrancyGuard&&) = delete;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return m_entered;
        }

    private:
        bool& m_active;
        bool m_entered{};
    };
}
