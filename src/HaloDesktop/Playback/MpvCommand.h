#pragma once

#include "Playback/IPlaybackEngine.h"

#include <string>
#include <vector>

struct mpv_handle;

namespace HaloDesktop::Playback
{
    // Shared libmpv call helpers. libmpv speaks UTF-8 and reports failure through
    // negative return codes, so every caller needs the same two conversions; they live
    // here rather than being repeated privately in each component that talks to mpv.
    [[nodiscard]] std::string EncodeMpvUtf8(std::wstring const& value);
    void CheckMpvResult(char const* operation, int result);
    void RunMpvCommand(mpv_handle* handle, std::vector<std::string> const& arguments);

    void LoadMpvSource(mpv_handle* handle, PlaybackSource const& source);
}
