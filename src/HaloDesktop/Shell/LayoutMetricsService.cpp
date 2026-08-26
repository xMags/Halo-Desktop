#include "pch.h"
#include "Shell/LayoutMetricsService.h"

#include <utility>
#include <vector>

namespace HaloDesktop::Shell
{
    namespace
    {
        // Measured against the content area, so these are the widths a page has
        // to lay shelves out in, not the size of the window around it.
        constexpr double RegularFrom = 1100.0;
        constexpr double WideFrom = 1500.0;

        constexpr double PosterAspect = 1.5;          // 2:3 artwork.
        constexpr double ContinueAspect = 9.0 / 16.0; // 16:9 stills.
    }

    double LayoutMetrics::PosterArtHeight() const noexcept
    {
        return PosterWidth * PosterAspect;
    }

    double LayoutMetrics::ContinueArtHeight() const noexcept
    {
        return ContinueWidth * ContinueAspect;
    }

    LayoutMetrics MetricsForContentWidth(double width) noexcept
    {
        if (width >= WideFrom)
        {
            return LayoutMetrics{ LayoutStep::Wide, 168.0, 336.0, 36.0, 420.0, 46.0 };
        }
        if (width >= RegularFrom)
        {
            return LayoutMetrics{ LayoutStep::Regular, 148.0, 300.0, 30.0, 360.0, 40.0 };
        }
        return LayoutMetrics{ LayoutStep::Compact, 132.0, 268.0, 24.0, 304.0, 36.0 };
    }

    double LargestPosterWidth() noexcept { return MetricsForContentWidth(WideFrom).PosterWidth; }
    double LargestContinueWidth() noexcept { return MetricsForContentWidth(WideFrom).ContinueWidth; }

    void LayoutMetricsService::SetContentWidth(double width)
    {
        // A width of zero means the shell has not been measured yet. Treating it
        // as compact would flip every page twice during startup.
        if (width <= 0.0)
        {
            return;
        }

        auto const next = MetricsForContentWidth(width);
        if (next == m_metrics)
        {
            return;
        }

        m_metrics = next;

        // Copied before dispatch: a handler is free to unregister itself, and
        // several of them do exactly that when their page unloads.
        std::vector<LayoutMetricsChangedHandler> handlers;
        handlers.reserve(m_handlers.size());
        for (auto const& entry : m_handlers)
        {
            handlers.push_back(entry.second);
        }
        for (auto const& handler : handlers)
        {
            handler();
        }
    }

    LayoutMetrics LayoutMetricsService::Current() const noexcept
    {
        return m_metrics;
    }

    LayoutMetricsChangedToken LayoutMetricsService::AddChangedHandler(LayoutMetricsChangedHandler handler)
    {
        if (!handler)
        {
            return 0;
        }

        auto const token = m_nextToken++;
        m_handlers.emplace(token, std::move(handler));
        return token;
    }

    void LayoutMetricsService::RemoveChangedHandler(LayoutMetricsChangedToken token) noexcept
    {
        m_handlers.erase(token);
    }
}
