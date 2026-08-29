#pragma once

#include "Playback/IPlaybackEngine.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace HaloDesktop::Playback
{
    // One decoded preview picture, already packed tightly as BGRA with an opaque alpha
    // channel so the UI can copy it straight into a bitmap. RequestId lets a late frame
    // from an abandoned scrub be recognised and dropped.
    struct ScrubPreviewFrame final
    {
        std::uint64_t RequestId{};
        double Seconds{};
        std::int32_t Width{};
        std::int32_t Height{};
        std::vector<std::uint8_t> Bgra;
    };

    using ScrubPreviewFrameHandler = std::function<void(ScrubPreviewFrame)>;

    // Decodes single frames at arbitrary timestamps without disturbing playback, so the
    // seek bar can show where a scrub is going. Implementations decode off the UI thread
    // and deliver frames back onto it.
    class IScrubPreviewSource
    {
    public:
        virtual ~IScrubPreviewSource() = default;
        // Records the source. Deliberately does no network work: a session where nobody
        // scrubs must never open a second connection to the origin.
        virtual void Open(PlaybackSource source) = 0;
        virtual void Close() noexcept = 0;
        // Latest request wins. A request that arrives while a decode is running
        // supersedes it rather than queueing behind it. Returns the id of the request
        // now considered current, which is the previous one when this one was folded
        // into it as too small a movement to be worth decoding.
        virtual std::uint64_t Request(double seconds) = 0;
        virtual void SetFrameHandler(ScrubPreviewFrameHandler handler) = 0;
        virtual void ClearFrameHandler() noexcept = 0;
    };
}
