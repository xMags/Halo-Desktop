#include "pch.h"
#include "Playback/NullScrubPreviewSource.h"

namespace HaloDesktop::Playback
{
    void NullScrubPreviewSource::Open([[maybe_unused]] PlaybackSource source)
    {
    }

    void NullScrubPreviewSource::Close() noexcept
    {
    }

    std::uint64_t NullScrubPreviewSource::Request([[maybe_unused]] double seconds)
    {
        return 0;
    }

    void NullScrubPreviewSource::SetFrameHandler([[maybe_unused]] ScrubPreviewFrameHandler handler)
    {
    }

    void NullScrubPreviewSource::ClearFrameHandler() noexcept
    {
    }
}
