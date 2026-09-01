#pragma once

#include "WrapPanel.g.h"

#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    // A left-to-right strip that moves to a new line when the next item will not
    // fit. WinUI ships no wrapping panel, and in the source sheet's detail tiles
    // the absence is structural rather than cosmetic: a horizontal StackPanel of
    // language chips reports a desired width its column cannot shrink below, and
    // a Grid star column widens to honour it, so one long chip run pulls the two
    // detail tiles off their equal halves.
    struct WrapPanel : WrapPanelT<WrapPanel>
    {
        WrapPanel() = default;

        [[nodiscard]] double HorizontalSpacing() const noexcept;
        void HorizontalSpacing(double value);
        [[nodiscard]] double VerticalSpacing() const noexcept;
        void VerticalSpacing(double value);

        Windows::Foundation::Size MeasureOverride(Windows::Foundation::Size const& available);
        Windows::Foundation::Size ArrangeOverride(Windows::Foundation::Size const& final);

    private:
        // Measure and arrange walk the children identically, so both run through
        // here and cannot drift into disagreeing about where a line breaks.
        Windows::Foundation::Size Layout(float width, bool arrange);

        double m_horizontalSpacing{};
        double m_verticalSpacing{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct WrapPanel : WrapPanelT<WrapPanel, implementation::WrapPanel> {};
}
