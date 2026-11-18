#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace HaloDesktop::Services
{
    // What Settings prints on its Version row, as the reference client's
    // `settings/about.ts` reads it: the app's own version and libmpv's release.

    // The executable's file version, from the VS_FIXEDFILEINFO inside a raw
    // RT_VERSION resource: `major.minor.build`, with `.revision` only when it
    // is not zero. Null when the block holds no valid fixed-info record.
    [[nodiscard]] std::optional<std::wstring> ParseFixedFileVersion(std::span<std::uint8_t const> resource);

    // mpv reports something like `mpv v0.41.0-878-g94335ab87`; the release
    // number is the part anyone reads. Anything unrecognisable is passed
    // through without a leading "mpv ", so an unusual build still identifies
    // itself. Null input or an empty string is unknown.
    [[nodiscard]] std::optional<std::wstring> MpvRelease(std::optional<std::string_view> raw);

    // `v{app} · mpv {mpv}`, with "…" for a part still being read and "unknown"
    // for one that could not be read. The reference prints a dash there; this
    // app's copy never uses that character.
    struct BuildInfoLineInput final
    {
        bool Loaded{};
        std::optional<std::wstring> App;
        std::optional<std::wstring> Mpv;
    };

    [[nodiscard]] std::wstring BuildInfoLine(BuildInfoLineInput const& input);
}
