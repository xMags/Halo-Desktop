#pragma once

namespace HaloDesktop::Controls
{
    // The reference client's shelf strip (`components/Shelf.tsx`): which ends
    // are in view, and where a chevron click lands.

    // Sub-pixel slack: a strip scrolled to its end can stop a fraction short.
    inline constexpr double ShelfEdgeSlack = 1.0;
    // The gap between cards on a strip.
    inline constexpr double ShelfCardGap = 14.0;

    struct ShelfEdges final
    {
        bool AtStart{ true };
        bool AtEnd{ true };

        bool operator==(ShelfEdges const&) const = default;
    };

    [[nodiscard]] ShelfEdges MeasureShelfEdges(double offset, double viewport, double extent) noexcept;

    // Pages by the whole cards that fit, landing on a card boundary so a click
    // never leaves a half card at either edge: the step is one card and its
    // gap, a page is as many steps as fit the viewport (at least one), and the
    // target is the nearest boundary to the current offset moved by one page.
    // Returns the offset unchanged when the card has no width.
    [[nodiscard]] double ShelfPageTarget(
        double offset,
        double viewport,
        double cardWidth,
        double gap,
        int direction) noexcept;
}
