#include "Services/ContinueShelfPolicy.h"

#include <algorithm>
#include <set>
#include <utility>

namespace HaloDesktop::Services
{
    namespace
    {
        // Below this, the viewer opened something and left almost immediately.
        // Such a row is not treated as having started, and deliberately does not
        // speak for its show, so a mis-click cannot hide a real row behind it.
        constexpr double MinimumStartedFraction = 0.02;
        // At or above this there is nothing left worth returning to, whether or
        // not the row carries the watched flag.
        constexpr double FinishedFraction = 0.95;
        constexpr wchar_t const* SeriesType = L"series";
        constexpr wchar_t const* MovieType = L"movie";
    }

    std::wstring TypeFromItemId(std::wstring_view itemId)
    {
        auto const separator = itemId.find(L':');
        return separator == std::wstring_view::npos ? MovieType : std::wstring{ itemId.substr(0, separator) };
    }

    std::wstring MetaIdFromItemId(std::wstring_view itemId)
    {
        auto const separator = itemId.find(L':');
        return std::wstring{ separator == std::wstring_view::npos ? itemId : itemId.substr(separator + 1) };
    }

    bool IsFinishedWatchRow(ContinueWatchRow const& row) noexcept
    {
        if (row.Watched)
        {
            return true;
        }
        return row.DurationSec > 0.0 && row.PositionSec / row.DurationSec >= FinishedFraction;
    }

    ContinueShelf BuildContinueShelf(
        std::vector<ContinueWatchRow> rows,
        std::function<NextEpisodeLookup(std::wstring const&)> const& resolvedNext,
        std::size_t maximumCards,
        std::size_t maximumRequests)
    {
        std::stable_sort(
            rows.begin(),
            rows.end(),
            [](ContinueWatchRow const& first, ContinueWatchRow const& second)
            {
                return first.UpdatedAt > second.UpdatedAt;
            });

        ContinueShelf shelf;
        std::set<std::wstring> claimed;
        for (auto const& row : rows)
        {
            if (shelf.Cards.size() >= maximumCards)
            {
                // Nothing further can be shown, and anything that could have
                // outranked what is here was already reached: the walk is in
                // newest-first order.
                break;
            }
            if (row.DurationSec <= 0.0 || row.Name.empty() || claimed.count(row.ItemId) != 0)
            {
                continue;
            }

            auto const finished = IsFinishedWatchRow(row);
            if (!finished && row.PositionSec / row.DurationSec <= MinimumStartedFraction)
            {
                continue;
            }

            // Past this point the row answers for its show, so an older row for
            // the same item cannot also produce a card, whether or not this one
            // ends up drawing anything.
            claimed.insert(row.ItemId);
            if (!finished)
            {
                shelf.Cards.push_back({
                    ContinueCardKind::Resume,
                    row.ItemId,
                    row.VideoId,
                    row.Name,
                    row.Poster,
                    row.PositionSec,
                    row.DurationSec,
                });
                continue;
            }

            // A finished film is simply finished. Only a series continues.
            auto type = TypeFromItemId(row.ItemId);
            if (type != SeriesType)
            {
                continue;
            }

            auto const next = resolvedNext ? resolvedNext(row.VideoId) : NextEpisodeLookup{};
            if (next.State == NextEpisodeState::Unknown)
            {
                if (shelf.Requests.size() < maximumRequests)
                {
                    shelf.Requests.push_back({
                        row.ItemId,
                        std::move(type),
                        MetaIdFromItemId(row.ItemId),
                        row.VideoId,
                    });
                }
                continue;
            }
            if (next.State == NextEpisodeState::None || next.NextVideoId.empty())
            {
                continue;
            }

            shelf.Cards.push_back({
                ContinueCardKind::NextEpisode,
                row.ItemId,
                next.NextVideoId,
                row.Name,
                row.Poster,
                0.0,
                0.0,
            });
        }
        return shelf;
    }
}
