#pragma once

#include <string>
#include <unordered_map>
#include <winrt/base.h>

namespace HaloDesktop::Services
{
    struct LandscapeArtworkSet final
    {
        winrt::hstring Background;
        std::unordered_map<std::wstring, winrt::hstring> Thumbnails;
    };

    // An episode still is more specific than the title backdrop. A caller keeps
    // its portrait poster as the final fallback because this policy deliberately
    // returns empty when metadata has no real landscape artwork.
    [[nodiscard]] inline winrt::hstring SelectLandscapeArtwork(
        winrt::hstring const& videoId,
        LandscapeArtworkSet const& artwork)
    {
        auto const thumbnail = artwork.Thumbnails.find(std::wstring{ videoId });
        if (thumbnail != artwork.Thumbnails.end() && !thumbnail->second.empty())
        {
            return thumbnail->second;
        }
        return artwork.Background;
    }
}
