#pragma once

#include "Api/Dto.h"
#include "Playback/IPlaybackEngine.h"

#include <cstdint>
#include <memory>
#include <optional>
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
        // Stages the position and sends everything staged.
        [[nodiscard]] concurrency::task<void> ReportAsync(PlaybackState state);
        // Keeps the position on this PC only, between reports. A position that has
        // not moved since the last stage or report is not written again.
        void Stage(PlaybackState const& state);

    private:
        // Where playback is now, as a history row; nothing while it is too short
        // or barely started to be worth one (the same rule for both paths).
        [[nodiscard]] std::optional<Api::Dto::WatchEntry> RowFor(PlaybackState const& state);

        std::shared_ptr<Services::WatchStateService> m_watchState;
        winrt::HaloDesktop::PlaybackRequest m_request{ nullptr };
        std::int64_t m_lastTimestamp{};
        std::optional<Api::Dto::WatchEntry> m_lastStaged;
    };
}
