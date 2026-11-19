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

    // The title page's rows (`components/ScrollRow.tsx`) share the shelf's
    // edges, but differ in three ways: their arrows appear only when the strip
    // overflows, a page moves the viewport less one card's worth (so the last
    // card seen stays as the anchor), and the edge with more to show fades over
    // this many pixels.
    inline constexpr double ScrollRowFadeWidth = 48.0;
    inline constexpr double ScrollRowPageInset = 160.0;

    // Whether the strip is wider than its viewport by more than the slack.
    [[nodiscard]] bool ShelfOverflows(double viewport, double extent) noexcept;

    // Whether a Home row draws its arrows. Drawn while it loads, and while its
    // strip has not been measured yet (an extent of zero), so a row that will
    // overflow never flashes them away and back; gone once a loaded row's
    // cards all fit, since there is nothing to page. A row that overflows
    // keeps them, dimmed at an end, as the reference's shelf does.
    [[nodiscard]] bool ShelfArrowsShown(bool loading, double viewport, double extent) noexcept;

    // Where an arrow click lands on a title page row: `max(viewport - 160, 160)`
    // further in `direction`, held inside the strip's range.
    [[nodiscard]] double ScrollRowPageTarget(double offset, double viewport, double extent, int direction) noexcept;

    // Whether a wheel turn over a sideways strip asks to move it sideways: a
    // tilt wheel (or a sideways touchpad swipe) or Shift held with the wheel. The
    // strips move by their own arrows only, so such a turn is taken and dropped;
    // a plain wheel is left alone to scroll the page the strip sits on.
    [[nodiscard]] bool IsSidewaysWheel(bool horizontalWheel, bool shiftHeld) noexcept;
}
