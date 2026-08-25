#pragma once

#include "Playback/IPlaybackEngine.h"

struct mpv_handle;

namespace HaloDesktop::Playback
{
    void LoadMpvSource(mpv_handle* handle, PlaybackSource const& source);
}
