#include "Controls/ShelfScrollPolicy.h"

#include <algorithm>
#include <cmath>

namespace HaloDesktop::Controls
{
    ShelfEdges MeasureShelfEdges(double offset, double viewport, double extent) noexcept
    {
        if (!std::isfinite(offset) || !std::isfinite(viewport) || !std::isfinite(extent))
        {
            return {};
        }
        return {
            offset <= ShelfEdgeSlack,
            offset + viewport >= extent - ShelfEdgeSlack,
        };
    }

    double ShelfPageTarget(double offset, double viewport, double cardWidth, double gap, int direction) noexcept
    {
        auto const step = cardWidth + gap;
        if (!std::isfinite(step) || step <= 0.0 || !std::isfinite(offset) || !std::isfinite(viewport))
        {
            return offset;
        }
        auto const perPage = (std::max)(1.0, std::floor((viewport + gap) / step));
        // JavaScript's Math.round: halves go up, toward positive infinity.
        auto const current = std::floor(offset / step + 0.5);
        auto const sign = direction < 0 ? -1.0 : 1.0;
        return (current + sign * perPage) * step;
    }

    bool ShelfOverflows(double viewport, double extent) noexcept
    {
        if (!std::isfinite(viewport) || !std::isfinite(extent)) return false;
        return extent - viewport > ShelfEdgeSlack;
    }

    double ScrollRowPageTarget(double offset, double viewport, double extent, int direction) noexcept
    {
        if (!std::isfinite(offset) || !std::isfinite(viewport) || !std::isfinite(extent))
        {
            return 0.0;
        }
        auto const step = (std::max)(viewport - ScrollRowPageInset, ScrollRowPageInset);
        auto const sign = direction < 0 ? -1.0 : 1.0;
        auto const end = (std::max)(0.0, extent - viewport);
        return std::clamp(offset + sign * step, 0.0, end);
    }

    bool ShelfArrowsShown(bool loading, double viewport, double extent) noexcept
    {
        if (loading || !(extent > 0.0)) return true;
        return ShelfOverflows(viewport, extent);
    }

    bool IsSidewaysWheel(bool horizontalWheel, bool shiftHeld) noexcept
    {
        return horizontalWheel || shiftHeld;
    }
}
