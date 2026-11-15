#pragma once

#include <vector>

namespace HaloDesktop::Controls
{
    // The Halo mark, drawn on a 64-unit square: an H whose crossbar is a tilted ring
    // passing behind the H at the top and in front of it at the bottom. A port of the
    // reference client's HaloMark.tsx.
    //
    // The reference cuts the front ring out of the bars with an SVG mask. XAML has no
    // masks, so the bars are computed here already cut: each bar is split into the
    // piece above the front ring's clearance band and the piece below it, as plain
    // polygons the control fills. Pure geometry, no XAML, so the tests can include it.
    namespace HaloMarkMetrics
    {
        inline constexpr double ViewBox = 64.0;
        // At or below this the ring would thin past a pixel, so it is drawn heavier
        // and the bars keep a narrower gap, or the gap cuts the H into four pieces.
        inline constexpr double SmallMarkSize = 24.0;
        inline constexpr double RingWidth = 5.0;
        inline constexpr double SmallRingWidth = 7.0;
        inline constexpr double RingClearance = 5.0;
        inline constexpr double SmallRingClearance = 3.0;
        // rotate(-16 32 34): the ring's tilt and the point it turns about.
        inline constexpr double RingTiltDegrees = -16.0;
        inline constexpr double RingCenterX = 32.0;
        inline constexpr double RingCenterY = 34.0;
        inline constexpr double RingRadiusX = 27.0;
        inline constexpr double RingRadiusY = 9.5;
        // The bars: x 13 and 39, y 7, 12 wide, 50 tall, fully rounded ends.
        inline constexpr double BarWidth = 12.0;
        inline constexpr double BarTop = 7.0;
        inline constexpr double BarHeight = 50.0;
        inline constexpr double LeftBarX = 13.0;
        inline constexpr double RightBarX = 39.0;
    }

    enum class HaloMarkWeight
    {
        // Above SmallMarkSize: a 5-unit ring standing 5 units clear of the bars.
        Regular,
        // At or below SmallMarkSize: a 7-unit ring standing 3 units clear.
        Small,
    };

    struct MarkPoint final
    {
        double X{};
        double Y{};
    };

    // Closed polygons in view-box units, each filled on its own.
    using MarkPolygon = std::vector<MarkPoint>;

    [[nodiscard]] HaloMarkWeight HaloMarkWeightFor(double size) noexcept;
    [[nodiscard]] double HaloMarkRingWidth(HaloMarkWeight weight) noexcept;
    [[nodiscard]] double HaloMarkRingClearance(HaloMarkWeight weight) noexcept;

    // A point on the front half of the tilted ring, `fraction` 0 at its left end
    // and 1 at its right end, in view-box units.
    [[nodiscard]] MarkPoint HaloMarkFrontRingPoint(double fraction) noexcept;

    // The two bars with the front ring's band (ring width plus clearance, round
    // capped) removed: four polygons, top and bottom piece of the left bar, then of
    // the right bar.
    [[nodiscard]] std::vector<MarkPolygon> HaloMarkBarPieces(HaloMarkWeight weight);
}
