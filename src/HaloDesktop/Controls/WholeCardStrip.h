#pragma once

#include "Shell/LayoutMetricsService.h"

#include <memory>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Controls
{
    // Sizes the cards of one Home strip so a whole number of them fill it
    // exactly (WholeCardWidth): none is ever cut at the strip's edge, and a page
    // moves by exactly the cards in view. Each item template's root is a plain
    // panel this sets the width of, with a card inside that stretches to it and
    // keeps its own art aspect.
    //
    // The minimum width is the layout step's (`minimumWidth` picks the poster or
    // the continue width), so it follows both the strip's size and the step.
    //
    // UI-thread-only. Owned by the control whose strip it sizes; not copyable,
    // because its handlers refer back to it, and every handler is revoked or
    // removed before the rest of it is destroyed.
    class WholeCardStrip final
    {
    public:
        WholeCardStrip(
            winrt::Microsoft::UI::Xaml::Controls::ScrollViewer strip,
            winrt::Microsoft::UI::Xaml::Controls::ItemsRepeater items,
            std::shared_ptr<Shell::LayoutMetricsService> metrics,
            double Shell::LayoutMetrics::* minimumWidth,
            double gap);
        ~WholeCardStrip();

        WholeCardStrip(WholeCardStrip const&) = delete;
        WholeCardStrip& operator=(WholeCardStrip const&) = delete;
        WholeCardStrip(WholeCardStrip&&) = delete;
        WholeCardStrip& operator=(WholeCardStrip&&) = delete;

        // The width every card takes now: the paging step's card width.
        [[nodiscard]] double CardWidth() const noexcept;

    private:
        void Refresh() noexcept;
        void Size(winrt::Microsoft::UI::Xaml::UIElement const& element) const noexcept;

        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer m_strip;
        winrt::Microsoft::UI::Xaml::Controls::ItemsRepeater m_items;
        std::shared_ptr<Shell::LayoutMetricsService> m_metrics;
        double Shell::LayoutMetrics::* m_minimumWidth;
        double m_gap;
        double m_cardWidth;
        Shell::LayoutMetricsChangedToken m_metricsToken{};
        // Last, so they are revoked before the parts they reach.
        winrt::Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker m_stripSizeChanged;
        winrt::Microsoft::UI::Xaml::Controls::ItemsRepeater::ElementPrepared_revoker m_elementPrepared;
    };
}
