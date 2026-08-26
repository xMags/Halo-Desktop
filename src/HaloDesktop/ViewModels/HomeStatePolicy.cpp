#include "ViewModels/HomeStatePolicy.h"

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
