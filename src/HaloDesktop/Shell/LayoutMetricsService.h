#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace HaloDesktop::Shell
{
    // Three discrete steps rather than a continuous scale. Interpolating would
    // give fractional card widths, resample artwork at arbitrary sizes and make
    // the layout impossible to design against, because every window width would
    // produce a slightly different result.
    enum class LayoutStep
    {
        Compact,
        Regular,
        Wide,
    };

    // Sizes that follow the available width. Text metrics deliberately stop at
    // the hero title: body and label sizes are the user's DPI and text-size
    // settings to decide, not ours.
    struct LayoutMetrics final
    {
        LayoutStep Step{ LayoutStep::Compact };
        double PosterWidth{ 132.0 };
        double ContinueWidth{ 268.0 };
        double Gutter{ 24.0 };
        double HeroHeight{ 304.0 };
        double HeroTitleSize{ 36.0 };

        // Artwork aspect is fixed, so the heights are always derived rather than
        // stored, and a card can never end up with a mismatched frame.
        [[nodiscard]] double PosterArtHeight() const noexcept;
        [[nodiscard]] double ContinueArtHeight() const noexcept;

        bool operator==(LayoutMetrics const&) const = default;
    };

    using LayoutMetricsChangedToken = std::uint64_t;
    using LayoutMetricsChangedHandler = std::function<void()>;

    // UI-thread-only. The shell measures the area its pages actually get and
    // pushes it here; everything else reads the resulting step. Content width
    // rather than window width, because the nav rail is 48px collapsed and 280px
    // expanded, and that 232px has nothing to do with how large the window is.
    class LayoutMetricsService final
    {
    public:
        LayoutMetricsService() = default;

        LayoutMetricsService(LayoutMetricsService const&) = delete;
        LayoutMetricsService& operator=(LayoutMetricsService const&) = delete;
        LayoutMetricsService(LayoutMetricsService&&) = delete;
        LayoutMetricsService& operator=(LayoutMetricsService&&) = delete;

        // Ignored when it does not change the step, so a drag-resize raises at
        // most two notifications instead of one per frame.
        void SetContentWidth(double width);
        [[nodiscard]] LayoutMetrics Current() const noexcept;

        LayoutMetricsChangedToken AddChangedHandler(LayoutMetricsChangedHandler handler);
        void RemoveChangedHandler(LayoutMetricsChangedToken token) noexcept;

    private:
        LayoutMetrics m_metrics{};
        std::unordered_map<LayoutMetricsChangedToken, LayoutMetricsChangedHandler> m_handlers;
        LayoutMetricsChangedToken m_nextToken{ 1 };
    };

    [[nodiscard]] LayoutMetrics MetricsForContentWidth(double width) noexcept;
}
