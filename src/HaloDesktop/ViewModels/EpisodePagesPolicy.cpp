#include "ViewModels/EpisodePagesPolicy.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace
{
    // The reference labels ranges with an en dash.
    constexpr wchar_t RangeDash[] = L"\x2013";

    std::unordered_map<std::wstring_view, HaloDesktop::ViewModels::EpisodeRef const*> ById(
        std::span<HaloDesktop::ViewModels::EpisodeRef const> videos)
    {
        std::unordered_map<std::wstring_view, HaloDesktop::ViewModels::EpisodeRef const*> result;
        result.reserve(videos.size());
        for (auto const& video : videos)
        {
            result.try_emplace(std::wstring_view{ video.VideoId }, &video);
        }
        return result;
    }
}

namespace HaloDesktop::ViewModels
{
    std::vector<EpisodePage> EpisodePages(std::span<EpisodeRef const> episodes, std::size_t size)
    {
        std::vector<EpisodePage> pages;
        if (size == 0) size = EpisodePageSize;
        for (std::size_t start{}; start < episodes.size(); start += size)
        {
            auto const count = (std::min)(size, episodes.size() - start);
            auto const first = episodes[start].Number.value_or(static_cast<std::int32_t>(start + 1));
            auto const last = episodes[start + count - 1].Number.value_or(static_cast<std::int32_t>(start + count));
            auto label = first == last
                ? L"Episode " + std::to_wstring(first)
                : L"Episodes " + std::to_wstring(first) + RangeDash + std::to_wstring(last);
            pages.push_back({ std::move(label), start, count });
        }
        return pages;
    }

    std::size_t PageContaining(
        std::span<EpisodePage const> pages,
        std::span<EpisodeRef const> episodes,
        std::optional<std::wstring_view> videoId)
    {
        if (!videoId || videoId->empty()) return 0;
        for (std::size_t index{}; index < pages.size(); ++index)
        {
            auto const& page = pages[index];
            for (std::size_t offset{}; offset < page.Count && page.First + offset < episodes.size(); ++offset)
            {
                if (episodes[page.First + offset].VideoId == *videoId) return index;
            }
        }
        return 0;
    }

    std::vector<std::int32_t> SeasonOrder(std::span<EpisodeRef const> videos)
    {
        std::vector<std::int32_t> seasons;
        std::unordered_set<std::int32_t> seen;
        for (auto const& video : videos)
        {
            auto const season = video.Season.value_or(0);
            if (seen.insert(season).second) seasons.push_back(season);
        }
        std::sort(seasons.begin(), seasons.end(), [](std::int32_t left, std::int32_t right)
        {
            if (left == right) return false;
            if (left == 0) return false;
            if (right == 0) return true;
            return left < right;
        });
        return seasons;
    }

    std::size_t NumberedSeasonCount(std::span<std::int32_t const> seasons) noexcept
    {
        return static_cast<std::size_t>(std::count_if(seasons.begin(), seasons.end(), [](std::int32_t season)
        {
            return season != 0;
        }));
    }

    std::optional<Api::Dto::WatchEntry> ResumeTarget(
        std::span<Api::Dto::WatchEntry const> rows,
        std::span<EpisodeRef const> videos)
    {
        auto const videosById = ById(videos);
        Api::Dto::WatchEntry const* newest{};
        for (auto const& row : rows)
        {
            if (row.Watched || !(row.DurationSec > 0.0)) continue;
            if (!videos.empty() && !videosById.contains(std::wstring_view{ row.VideoId })) continue;
            // Strictly newer, so the first of equally stamped rows wins, as a
            // stable sort newest-first would leave it.
            if (!newest || row.UpdatedAt > newest->UpdatedAt) newest = &row;
        }
        return newest ? std::optional<Api::Dto::WatchEntry>{ *newest } : std::nullopt;
    }

    std::optional<std::int32_t> LastWatchedSeason(
        std::span<Api::Dto::WatchEntry const> rows,
        std::span<EpisodeRef const> videos)
    {
        auto const videosById = ById(videos);
        Api::Dto::WatchEntry const* newest{};
        EpisodeRef const* newestVideo{};
        for (auto const& row : rows)
        {
            auto const found = videosById.find(std::wstring_view{ row.VideoId });
            if (found == videosById.end()) continue;
            if (!row.Watched && !(row.PositionSec > 0.0)) continue;
            if (!newest || row.UpdatedAt > newest->UpdatedAt)
            {
                newest = &row;
                newestVideo = found->second;
            }
        }
        return newestVideo ? newestVideo->Season : std::nullopt;
    }
}
