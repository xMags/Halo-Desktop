#include "Playback/ScrubPreviewPolicy.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace HaloDesktop::Playback
{
    ScrubPreviewTime ScrubPreviewTimeFromPointer(
        double pointerX,
        double trackWidth,
        double thumbWidth,
        double durationSeconds) noexcept
    {
        if (!std::isfinite(pointerX) || !std::isfinite(trackWidth) || !std::isfinite(thumbWidth)
            || !std::isfinite(durationSeconds) || durationSeconds <= 0.0 || thumbWidth < 0.0)
        {
            return {};
        }

        auto const usableWidth = trackWidth - thumbWidth;
        if (usableWidth <= 0.0)
        {
            return {};
        }

        auto const fraction = std::clamp((pointerX - thumbWidth / 2.0) / usableWidth, 0.0, 1.0);
        return { .Valid = true, .Seconds = fraction * durationSeconds };
    }

    double ClampScrubPreviewOffset(double pointerX, double cardWidth, double hostWidth) noexcept
    {
        if (!std::isfinite(pointerX) || !std::isfinite(cardWidth) || !std::isfinite(hostWidth)
            || cardWidth <= 0.0)
        {
            return 0.0;
        }

        auto const rightmost = hostWidth - cardWidth;
        if (rightmost <= 0.0)
        {
            return 0.0;
        }
        return std::clamp(pointerX - cardWidth / 2.0, 0.0, rightmost);
    }

    bool ShouldIssueScrubPreview(
        double requestedSeconds,
        double lastIssuedSeconds,
        bool hasIssued) noexcept
    {
        if (!std::isfinite(requestedSeconds) || requestedSeconds < 0.0)
        {
            return false;
        }
        if (!hasIssued || !std::isfinite(lastIssuedSeconds))
        {
            return true;
        }
        return std::abs(requestedSeconds - lastIssuedSeconds) >= ScrubPreviewMinimumDeltaSeconds;
    }

    std::span<ScrubPreviewOption const> ScrubPreviewMpvOptions() noexcept
    {
        static constexpr std::array Options{
            ScrubPreviewOption{ "vo", "null" },
            ScrubPreviewOption{ "ao", "null" },
            ScrubPreviewOption{ "audio", "no" },
            ScrubPreviewOption{ "sid", "no" },
            ScrubPreviewOption{ "sub-auto", "no" },
            ScrubPreviewOption{ "idle", "yes" },
            ScrubPreviewOption{ "terminal", "no" },
            ScrubPreviewOption{ "force-window", "no" },
            ScrubPreviewOption{ "pause", "yes" },
            ScrubPreviewOption{ "ytdl", "no" },
            // Hardware decoding would cost a second device context and still needs a
            // readback for every grab. One downscaled keyframe is cheaper in software,
            // and it leaves the playing engine's GPU pipeline uncontended.
            ScrubPreviewOption{ "hwdec", "no" },
            // The playing engine deliberately avoids keep-open because it suppresses the
            // EOF event up-next is driven by. This instance has no such contract, and
            // without it a preview of the last seconds unloads the file.
            ScrubPreviewOption{ "keep-open", "yes" },
            ScrubPreviewOption{ "demuxer-max-bytes", "8MiB" },
            ScrubPreviewOption{ "demuxer-readahead-secs", "1" },
            ScrubPreviewOption{ "cache-on-disk", "no" },
        };
        return Options;
    }

    std::span<char const* const> ScrubPreviewScaleFilters() noexcept
    {
        static constexpr std::array Filters{
            "scale=320:-2",
            "lavfi=[scale=320:-2]",
        };
        return Filters;
    }
}
