#pragma once

#include "Api/Dto.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <winrt/base.h>

namespace HaloDesktop::Services
{
    // Which file of a resolved magnet stands for a video of the title page,
    // and how the Sources sheet names it. Pure: compiles without XAML.
    //
    // A magnet's files are never listed. A ready, identified magnet is offered
    // only as a source of the title it was matched to:
    // - a film (`movie`, video id equal to the matched `tt` id): its largest file;
    // - an episode (`series`, video id `tt...:season:episode` of the matched
    //   id): the largest file the server read that same season and episode
    //   from. A file without both numbers is never offered for an episode.
    // Anything else (not ready, unidentified, another title) offers nothing.

    // The biggest file, the first of equals; none for an empty list.
    [[nodiscard]] std::optional<std::size_t> LargestMagnetFile(std::span<::HaloDesktop::Api::Dto::MagnetFile const> files) noexcept;

    [[nodiscard]] std::optional<::HaloDesktop::Api::Dto::MagnetFile> MagnetFileFor(
        ::HaloDesktop::Api::Dto::MagnetRequest const& magnet,
        std::wstring_view type,
        std::wstring_view videoId);

    // The Sources sheet's first row for that file, above the ranked sources.
    struct MagnetSourceRow final
    {
        winrt::hstring Name;
        winrt::hstring FileName;
        // `8.42 GB`, or UNKNOWN when the server gave no size.
        winrt::hstring Size;

        bool operator==(MagnetSourceRow const&) const = default;
    };

    inline constexpr wchar_t MagnetSourceName[] = L"Your magnet";

    [[nodiscard]] MagnetSourceRow BuildMagnetSourceRow(::HaloDesktop::Api::Dto::MagnetFile const& file);
}
