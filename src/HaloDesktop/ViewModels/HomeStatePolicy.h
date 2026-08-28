#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace HaloDesktop::ViewModels
{
    enum class HomeFilter
    {
        All,
        Movies,
        Series,
    };

    enum class HomeMediaKind
    {
        Movie,
        Series,
    };

    struct HomeVisibilityState final
    {
        bool ShowContent{};
        bool ShowLoading{};
        bool ShowError{};
        bool ShowEmpty{};
    };

    [[nodiscard]] HomeFilter HomeFilterFromIndex(std::int32_t index) noexcept;
    [[nodiscard]] bool MatchesHomeFilter(HomeFilter filter, HomeMediaKind kind) noexcept;
    [[nodiscard]] std::optional<std::size_t> FirstMatchingHomeItem(
        HomeFilter filter,
        std::span<HomeMediaKind const> kinds) noexcept;
    // One title the home carousel could feature, reduced to what decides
    // whether it earns a slot and where.
    struct FeaturedCandidate final
    {
        // Type and id together, so the same title carried by two catalogs is
        // recognised as one. An empty key is never featured: the carousel's
        // buttons cannot open a title they cannot address.
        std::wstring Key;
        HomeMediaKind Kind{};
        bool HasBackdrop{};
    };

    // Picks up to count titles for the carousel, in catalog order, never
    // repeating one that several catalogs both list. Titles with a backdrop go
    // first: the carousel draws them full width, and a poster stretched to that
    // shape looks wrong. The rest only top the strip up when there are too few
    // backdrops to fill it.
    [[nodiscard]] std::vector<std::size_t> SelectFeaturedItems(
        HomeFilter filter,
        std::span<FeaturedCandidate const> candidates,
        std::size_t count);

    [[nodiscard]] HomeVisibilityState ResolveHomeVisibility(
        bool loading,
        bool error,
        bool hasUsableContent) noexcept;
} // namespace HaloDesktop::ViewModels
