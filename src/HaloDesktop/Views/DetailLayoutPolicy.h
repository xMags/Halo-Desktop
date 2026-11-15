#pragma once

namespace HaloDesktop::Views
{
    // The title page's responsive metrics, from the reference's `detail.css`.
    // Every rule is measured on the content column (the page's own width), as
    // the reference's container queries are. Pure: no XAML types.

    // The compact bar's height, which is also how far below the page's top the
    // main button counts as scrolled away.
    inline constexpr double DetailCompactBarHeight = 64.0;
    // The "Ways to watch" aside's width.
    inline constexpr double DetailAsideWidth = 340.0;
    // About's column beside the cast panel.
    inline constexpr double DetailAboutWidth = 360.0;

    struct DetailLayout final
    {
        // The page gutter: 32px, 48px from 1100px, 56px from 1500px.
        double Gutter{};
        // The aside is hidden at 940px and below.
        bool ShowAside{};
        // Cast and About sit side by side above 1040px, else one column.
        bool SplitSideBySide{};

        bool operator==(DetailLayout const&) const = default;
    };

    [[nodiscard]] DetailLayout DetailLayoutFor(double contentWidth) noexcept;

    // The hero (and the sharp art behind it): 74% of the view, at least 480px
    // and at most 780px.
    [[nodiscard]] double DetailHeroHeight(double viewportHeight) noexcept;

    // Whether the compact bar shows: the main button has scrolled wholly above
    // the band the bar covers. A button below the view (a short window) is not
    // "scrolled past". `buttonTop` is measured from the page scroller's top.
    [[nodiscard]] bool CompactBarShown(double buttonTop, double buttonHeight) noexcept;
}
