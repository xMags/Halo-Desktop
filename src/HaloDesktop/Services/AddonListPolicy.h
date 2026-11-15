#pragma once

#include <array>
#include <span>
#include <string_view>

namespace HaloDesktop::Services
{
    // Addons installed on first sign-in so the app works before the user adds
    // anything: Cinemeta for catalogs and metadata, OpenSubtitles for subtitles
    // (both the official Stremio-operated instances), and The Movie Database
    // addon for TMDB's catalogs and pictured cast, as ElfHosted's unconfigured
    // public instance. A configured URL carries its owner's settings and keys,
    // so one must never be built in.
    inline constexpr std::array<std::wstring_view, 3> DefaultAddonUrls{
        L"https://v3-cinemeta.strem.io/manifest.json",
        L"https://opensubtitles-v3.strem.io/manifest.json",
        L"https://tmdb.elfhosted.com/manifest.json",
    };

    [[nodiscard]] bool IsDefaultAddonUrl(std::wstring_view transportUrl) noexcept;

    struct AddonEditInput final
    {
        bool IsGlobal{};
        bool HasTransportUrl{};
    };

    // Which of the two lists can be saved back. A save re-sends the whole list
    // of its scope as transport URLs, so it needs every URL of that scope and
    // none of the other: the server never sends global URLs to a non-admin,
    // and that must not lock them out of their own list.
    struct AddonListEditability final
    {
        bool User{};
        bool Global{};
    };

    [[nodiscard]] AddonListEditability EvaluateAddonEditability(
        std::span<AddonEditInput const> addons,
        bool isAdmin) noexcept;

    [[nodiscard]] bool CanEditAddonInScope(AddonListEditability editability, bool isGlobal) noexcept;
}
