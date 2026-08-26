#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

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
    [[nodiscard]] HomeVisibilityState ResolveHomeVisibility(
        bool loading,
        bool error,
        bool hasUsableContent) noexcept;
} // namespace HaloDesktop::ViewModels
