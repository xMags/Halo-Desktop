#pragma once

#include "Playback/IPlaybackEngine.h"

#include <cstdint>
#include <memory>
#include <ppltasks.h>
#include <winrt/HaloDesktop.h>

namespace HaloDesktop::Services
{
    class WatchStateService;
}

namespace HaloDesktop::Playback
{
    // UI-thread-only. Timestamp allocation happens before the network await so
    // overlapping reports remain strictly ordered at the server boundary.
    class WatchReporter final
    {
    public:
        WatchReporter(std::shared_ptr<Services::WatchStateService> watchState,winrt::HaloDesktop::PlaybackRequest request);
        [[nodiscard]] concurrency::task<void> ReportAsync(PlaybackState state);

    private:
        std::shared_ptr<Services::WatchStateService> m_watchState;
        winrt::HaloDesktop::PlaybackRequest m_request{ nullptr };
        std::int64_t m_lastTimestamp{};
    };
}
