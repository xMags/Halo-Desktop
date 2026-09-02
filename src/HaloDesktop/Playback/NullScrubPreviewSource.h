#pragma once

#include "Playback/IScrubPreviewSource.h"

namespace HaloDesktop::Playback
{
    // Stands in wherever libmpv is not linked (unsupported architectures and the null
    // playback configuration). Previews are an enhancement, so absence is silent: the seek bar
    // still shows the timestamp, just never a picture.
    class NullScrubPreviewSource final : public IScrubPreviewSource
    {
    public:
        void Open(PlaybackSource source) override;
        void Close() noexcept override;
        std::uint64_t Request(double seconds) override;
        void SetFrameHandler(ScrubPreviewFrameHandler handler) override;
        void ClearFrameHandler() noexcept override;
    };
}
