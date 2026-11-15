#pragma once

#include "Api/Dto.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::ViewModels
{
    // The title page's season and episode rules: the reference client's
    // `browse/detail/episodePages.ts` and the season, resume and opening rules
    // of `browse/detail/Detail.tsx`. Pure: no XAML types.

    // Above this many episodes a season is shown a page at a time.
    inline constexpr std::size_t EpisodePageSize = 25;

    // An episode as paging and the opening rules see it.
    struct EpisodeRef final
    {
        std::wstring VideoId;
        // The episode's own number, when the addon sent one.
        std::optional<std::int32_t> Number;
        // Its season, when the addon sent one.
        std::optional<std::int32_t> Season;
    };

    struct EpisodePage final
    {
        // "Episodes 1–25", by the episodes' own numbers where they have them.
        std::wstring Label;
        // The page's run of the season's episodes.
        std::size_t First{};
        std::size_t Count{};
    };

    // A season's episodes in pages of `size`, in order. A season of up to
    // `size` episodes is one page, which the panel shows without a picker.
    [[nodiscard]] std::vector<EpisodePage> EpisodePages(std::span<EpisodeRef const> episodes, std::size_t size = EpisodePageSize);

    // The page holding `videoId`, or the first page when no page does.
    [[nodiscard]] std::size_t PageContaining(
        std::span<EpisodePage const> pages,
        std::span<EpisodeRef const> episodes,
        std::optional<std::wstring_view> videoId);

    // The seasons a title's videos fall in (a video without one counts as
    // season 0), ascending, with Specials (season 0) last.
    [[nodiscard]] std::vector<std::int32_t> SeasonOrder(std::span<EpisodeRef const> videos);

    // How many of those are numbered seasons, which a show's facts line counts.
    [[nodiscard]] std::size_t NumberedSeasonCount(std::span<std::int32_t const> seasons) noexcept;

    // The episode the viewer is actually mid-way through: the newest of the
    // title's watch rows that is not watched, has a length, and names one of
    // the title's videos (any row of the title when it lists no videos, as a
    // film does). There are no percentage thresholds. Rows must already be the
    // title's own (`itemId` matched).
    [[nodiscard]] std::optional<Api::Dto::WatchEntry> ResumeTarget(
        std::span<Api::Dto::WatchEntry const> rows,
        std::span<EpisodeRef const> videos);

    // The season the page opens on: the season of the most recently watched
    // episode (watched, or with a position), when that episode names one.
    // Empty otherwise, and the page then opens on its first season. An episode
    // marked unwatched records no viewing, so it does not count.
    [[nodiscard]] std::optional<std::int32_t> LastWatchedSeason(
        std::span<Api::Dto::WatchEntry const> rows,
        std::span<EpisodeRef const> videos);
}
