#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <utility>

namespace HaloDesktop::Services::Downloads
{
    // Averages one transfer's throughput over a sliding time window. A single
    // progress slice exaggerates TCP bursts and disk stalls; the window reports
    // what the transfer actually sustained.
    class TransferRateWindow final
    {
    public:
        using Clock = std::chrono::steady_clock;

        explicit TransferRateWindow(Clock::duration window);

        // Forgets all history and starts again from this byte count.
        void Reset(Clock::time_point now, std::uint64_t bytes);

        // Records the running byte count of the transfer and returns the average
        // bytes per second over the retained window. Returns 0 until a second
        // sample exists.
        [[nodiscard]] std::uint64_t Record(Clock::time_point now, std::uint64_t bytes);

    private:
        Clock::duration m_window;
        std::deque<std::pair<Clock::time_point, std::uint64_t>> m_samples;
    };
} // namespace HaloDesktop::Services::Downloads
