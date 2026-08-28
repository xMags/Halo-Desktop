#include "ViewModels/HomeStatePolicy.h"

#include <unordered_set>

namespace HaloDesktop::ViewModels
{
    HomeFilter HomeFilterFromIndex(std::int32_t index) noexcept
    {
        switch (index)
        {
        case 1:
            return HomeFilter::Movies;
        case 2:
            return HomeFilter::Series;
        default:
            return HomeFilter::All;
        }
    }

    bool MatchesHomeFilter(HomeFilter filter, HomeMediaKind kind) noexcept
    {
        return filter == HomeFilter::All ||
            (filter == HomeFilter::Movies && kind == HomeMediaKind::Movie) ||
            (filter == HomeFilter::Series && kind == HomeMediaKind::Series);
    }

    std::optional<std::size_t> FirstMatchingHomeItem(
        HomeFilter filter,
        std::span<HomeMediaKind const> kinds) noexcept
    {
        for (std::size_t index = 0; index < kinds.size(); ++index)
        {
            if (MatchesHomeFilter(filter, kinds[index]))
            {
                return index;
            }
        }
        return std::nullopt;
    }

    std::vector<std::size_t> SelectFeaturedItems(
        HomeFilter filter,
        std::span<FeaturedCandidate const> candidates,
        std::size_t count)
    {
        std::vector<std::size_t> selected;
        if (count == 0)
        {
            return selected;
        }
        selected.reserve(count);
        std::unordered_set<std::wstring> seen;
        // Two ordered passes rather than a sort: catalog order already carries
        // the ranking each addon intended, and this keeps it inside both groups.
        for (auto const wantBackdrop : { true, false })
        {
            for (std::size_t index = 0; index < candidates.size(); ++index)
            {
                if (selected.size() == count)
                {
                    return selected;
                }
                auto const& candidate = candidates[index];
                if (candidate.HasBackdrop != wantBackdrop
                    || candidate.Key.empty()
                    || !MatchesHomeFilter(filter, candidate.Kind))
                {
                    continue;
                }
                if (seen.insert(candidate.Key).second)
                {
                    selected.push_back(index);
                }
            }
        }
        return selected;
    }

    HomeVisibilityState ResolveHomeVisibility(
        bool loading,
        bool error,
        bool hasUsableContent) noexcept
    {
        return {
            hasUsableContent,
            loading && !hasUsableContent,
            error && !hasUsableContent,
            !loading && !error && !hasUsableContent,
        };
    }
} // namespace HaloDesktop::ViewModels
