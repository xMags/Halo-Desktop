#include "Views/DetailLayoutPolicy.h"

#include <algorithm>
#include <cmath>

namespace HaloDesktop::Views
{
    DetailLayout DetailLayoutFor(double contentWidth) noexcept
    {
        if (!std::isfinite(contentWidth) || contentWidth < 0.0) contentWidth = 0.0;
        DetailLayout layout;
        layout.Gutter = contentWidth >= 1500.0 ? 56.0 : contentWidth >= 1100.0 ? 48.0 : 32.0;
        layout.ShowAside = contentWidth > 940.0;
        layout.SplitSideBySide = contentWidth > 1040.0;
        return layout;
    }

    double DetailHeroHeight(double viewportHeight) noexcept
    {
        if (!std::isfinite(viewportHeight) || viewportHeight < 0.0) viewportHeight = 0.0;
        return std::clamp(viewportHeight * 0.74, 480.0, 780.0);
    }

    bool CompactBarShown(double buttonTop, double buttonHeight) noexcept
    {
        if (!std::isfinite(buttonTop) || !std::isfinite(buttonHeight)) return false;
        // An intersection observer counts an edge that only touches the band as
        // still intersecting, so the bottom has to be strictly above it.
        return buttonTop + buttonHeight < DetailCompactBarHeight;
    }
}
