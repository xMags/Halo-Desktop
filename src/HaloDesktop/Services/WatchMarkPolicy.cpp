#include "Services/WatchMarkPolicy.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace HaloDesktop::Services
{
    namespace
    {
        // The server refuses a longer name; the player's watch reporter trims to the
        // same length.
        constexpr std::size_t MaximumNameLength = 512;

        [[nodiscard]] bool IsHighSurrogate(wchar_t unit) noexcept
        {
            return unit >= 0xD800 && unit <= 0xDBFF;
        }

        [[nodiscard]] winrt::hstring BoundedName(winrt::hstring const& name)
        {
            std::wstring_view const text{ name };
            if (text.size() <= MaximumNameLength)
            {
                return name;
            }
            auto length = MaximumNameLength;
            // Never cut a character in half.
            if (IsHighSurrogate(text[length - 1]))
            {
                --length;
            }
            return winrt::hstring{ text.substr(0, length) };
        }

        [[nodiscard]] std::int64_t NewerThan(
            std::optional<Api::Dto::WatchEntry> const& existing,
            std::int64_t now) noexcept
        {
            if (!existing || existing->UpdatedAt < now)
            {
                return now;
            }
            // Strictly newer than the row it replaces, or the server keeps the old
            // one: another device's clock may have run ahead of this one.
            return existing->UpdatedAt == (std::numeric_limits<std::int64_t>::max)()
                ? existing->UpdatedAt
                : existing->UpdatedAt + 1;
        }

        [[nodiscard]] Api::Dto::WatchEntry RowBase(
            WatchMarkTarget const& target,
            std::optional<Api::Dto::WatchEntry> const& existing,
            std::int64_t now)
        {
            Api::Dto::WatchEntry row;
            row.VideoId = target.VideoId;
            row.ItemId = target.ItemId;
            // The server refuses an empty name, and a newer row must not lose the
            // display fields an older one carried.
            if (!target.Name.empty())
            {
                row.Name = BoundedName(target.Name);
            }
            else if (existing && existing->Name && !existing->Name->empty())
            {
                row.Name = existing->Name;
            }
            if (!target.Poster.empty())
            {
                row.Poster = target.Poster;
            }
            else if (existing && existing->Poster && !existing->Poster->empty())
            {
                row.Poster = existing->Poster;
            }
            row.UpdatedAt = NewerThan(existing, now);
            return row;
        }
    }

    Api::Dto::WatchEntry WatchedRow(
        WatchMarkTarget const& target,
        std::optional<Api::Dto::WatchEntry> const& existing,
        double runtimeSeconds,
        std::int64_t now)
    {
        auto row = RowBase(target, existing, now);
        auto const runtime = std::isfinite(runtimeSeconds) && runtimeSeconds > 0.0 ? runtimeSeconds : 0.0;
        auto const duration = existing && existing->DurationSec > 0.0 ? existing->DurationSec : runtime;
        row.PositionSec = duration;
        row.DurationSec = duration;
        row.Watched = true;
        return row;
    }

    Api::Dto::WatchEntry UnwatchedRow(
        WatchMarkTarget const& target,
        std::optional<Api::Dto::WatchEntry> const& existing,
        std::int64_t now)
    {
        auto row = RowBase(target, existing, now);
        row.PositionSec = 0.0;
        row.DurationSec = 0.0;
        row.Watched = false;
        return row;
    }

    std::vector<Api::Dto::WatchEntry> WatchedRowsInOrder(
        std::vector<WatchMarkTarget> const& targets,
        std::vector<Api::Dto::WatchEntry> const& existingRows,
        double runtimeSeconds,
        std::int64_t now)
    {
        std::unordered_map<std::wstring_view, Api::Dto::WatchEntry const*> existing;
        existing.reserve(existingRows.size());
        for (auto const& row : existingRows)
        {
            existing.insert_or_assign(std::wstring_view{ row.VideoId }, &row);
        }

        std::vector<Api::Dto::WatchEntry> rows;
        rows.reserve(targets.size());
        auto stamp = now;
        for (auto const& target : targets)
        {
            auto const found = existing.find(std::wstring_view{ target.VideoId });
            auto const prior = found == existing.end()
                ? std::optional<Api::Dto::WatchEntry>{}
                : std::optional<Api::Dto::WatchEntry>{ *found->second };
            auto row = WatchedRow(target, prior, runtimeSeconds, stamp);
            stamp = row.UpdatedAt == (std::numeric_limits<std::int64_t>::max)()
                ? row.UpdatedAt
                : row.UpdatedAt + 1;
            rows.push_back(std::move(row));
        }
        return rows;
    }

    std::vector<Api::Dto::WatchEntry> RemovedRows(
        std::vector<Api::Dto::WatchEntry> const& rows,
        std::int64_t now)
    {
        std::vector<Api::Dto::WatchEntry> removed;
        removed.reserve(rows.size());
        for (auto const& row : rows)
        {
            auto tombstone = row;
            auto const stamp = NewerThan(row, now);
            tombstone.RemovedAt = stamp;
            tombstone.UpdatedAt = stamp;
            removed.push_back(std::move(tombstone));
        }
        return removed;
    }

    std::vector<Api::Dto::WatchEntry> HistoryRowsOf(
        std::vector<Api::Dto::WatchEntry> const& rows,
        winrt::hstring const& itemId)
    {
        std::vector<Api::Dto::WatchEntry> result;
        if (itemId.empty())
        {
            return result;
        }
        for (auto const& row : rows)
        {
            if (row.ItemId == itemId && !row.RemovedAt)
            {
                result.push_back(row);
            }
        }
        return result;
    }

    std::vector<Api::Dto::WatchEntry> HistoryRowsOfVideos(
        std::vector<Api::Dto::WatchEntry> const& rows,
        std::vector<winrt::hstring> const& videoIds)
    {
        std::unordered_set<std::wstring_view> wanted;
        for (auto const& videoId : videoIds)
        {
            if (!videoId.empty())
            {
                wanted.insert(std::wstring_view{ videoId });
            }
        }
        std::vector<Api::Dto::WatchEntry> result;
        if (wanted.empty())
        {
            return result;
        }
        for (auto const& row : rows)
        {
            if (!row.RemovedAt && wanted.contains(std::wstring_view{ row.VideoId }))
            {
                result.push_back(row);
            }
        }
        return result;
    }

    std::vector<Api::Dto::WatchEntry> LiveWatchRows(
        std::vector<Api::Dto::WatchEntry> const& rows)
    {
        std::vector<Api::Dto::WatchEntry> live;
        live.reserve(rows.size());
        for (auto const& row : rows)
        {
            if (!row.RemovedAt)
            {
                live.push_back(row);
            }
        }
        return live;
    }

    std::vector<std::vector<Api::Dto::WatchEntry>> ChunkRows(
        std::vector<Api::Dto::WatchEntry> const& rows,
        std::size_t size)
    {
        auto const step = size == 0 ? std::size_t{ 1 } : size;
        std::vector<std::vector<Api::Dto::WatchEntry>> chunks;
        chunks.reserve((rows.size() + step - 1) / step);
        for (std::size_t start = 0; start < rows.size(); start += step)
        {
            auto const end = start + (std::min)(step, rows.size() - start);
            chunks.emplace_back(
                rows.begin() + static_cast<std::ptrdiff_t>(start),
                rows.begin() + static_cast<std::ptrdiff_t>(end));
        }
        return chunks;
    }

    bool EchoesRemovedRows(
        std::vector<Api::Dto::WatchEntry> const& stored,
        std::vector<Api::Dto::WatchEntry> const& removed)
    {
        std::unordered_set<std::wstring_view> removedIds;
        removedIds.reserve(removed.size());
        for (auto const& row : removed)
        {
            removedIds.insert(std::wstring_view{ row.VideoId });
        }
        return std::any_of(stored.begin(), stored.end(), [&removedIds](Api::Dto::WatchEntry const& row)
        {
            return !row.RemovedAt && removedIds.contains(std::wstring_view{ row.VideoId });
        });
    }

    std::vector<std::size_t> EarlierEpisodes(
        std::vector<EpisodeSlot> const& episodes,
        std::size_t target)
    {
        std::vector<std::size_t> earlier;
        if (target >= episodes.size())
        {
            return earlier;
        }
        auto const& anchor = episodes[target];
        for (std::size_t index = 0; index < episodes.size(); ++index)
        {
            if (index == target)
            {
                continue;
            }
            auto const& other = episodes[index];
            auto const before = anchor.Season == 0
                ? other.Season == 0 && other.Number < anchor.Number
                : other.Season != 0
                    && (other.Season < anchor.Season
                        || (other.Season == anchor.Season && other.Number < anchor.Number));
            if (before)
            {
                earlier.push_back(index);
            }
        }
        std::stable_sort(earlier.begin(), earlier.end(), [&episodes](std::size_t first, std::size_t second)
        {
            auto const& a = episodes[first];
            auto const& b = episodes[second];
            return a.Season != b.Season ? a.Season < b.Season : a.Number < b.Number;
        });
        return earlier;
    }
}
