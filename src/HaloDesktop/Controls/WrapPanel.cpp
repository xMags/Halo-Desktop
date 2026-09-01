#include "pch.h"
#include "Controls/WrapPanel.h"
#if __has_include("WrapPanel.g.cpp")
#include "WrapPanel.g.cpp"
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace winrt::HaloDesktop::implementation
{
    double WrapPanel::HorizontalSpacing() const noexcept { return m_horizontalSpacing; }

    void WrapPanel::HorizontalSpacing(double value)
    {
        if (m_horizontalSpacing == value)
        {
            return;
        }
        m_horizontalSpacing = value;
        InvalidateMeasure();
    }

    double WrapPanel::VerticalSpacing() const noexcept { return m_verticalSpacing; }

    void WrapPanel::VerticalSpacing(double value)
    {
        if (m_verticalSpacing == value)
        {
            return;
        }
        m_verticalSpacing = value;
        InvalidateMeasure();
    }

    Windows::Foundation::Size WrapPanel::MeasureOverride(Windows::Foundation::Size const& available)
    {
        // Height is left unbounded: the panel is as tall as the lines it needs,
        // and every caller sits inside a vertically growing tile.
        Windows::Foundation::Size const budget{ available.Width, std::numeric_limits<float>::infinity() };
        for (auto const& child : Children())
        {
            child.Measure(budget);
        }
        return Layout(available.Width, false);
    }

    Windows::Foundation::Size WrapPanel::ArrangeOverride(Windows::Foundation::Size const& final)
    {
        auto const used = Layout(final.Width, true);
        // Reporting less than the slot the panel was handed makes the framework
        // centre it inside that slot, which would indent every wrapped strip
        // away from the caption above it. The panel owns the whole slot; the
        // children were already placed against its leading edge.
        return Windows::Foundation::Size{
            (std::max)(used.Width, final.Width),
            (std::max)(used.Height, final.Height),
        };
    }

    Windows::Foundation::Size WrapPanel::Layout(float width, bool arrange)
    {
        auto const horizontal = static_cast<float>(m_horizontalSpacing);
        auto const vertical = static_cast<float>(m_verticalSpacing);
        auto const limit = std::isfinite(width) ? width : std::numeric_limits<float>::infinity();

        std::vector<Microsoft::UI::Xaml::UIElement> line;
        float lineWidth{};
        float lineHeight{};
        float widest{};
        float top{};

        // Every item on a line is given the full height of that line, so a
        // VerticalAlignment set in markup means what it means everywhere else.
        auto const flush = [&]
        {
            if (arrange)
            {
                float left{};
                for (auto const& item : line)
                {
                    auto const size = item.DesiredSize();
                    item.Arrange(Windows::Foundation::Rect{ left, top, size.Width, lineHeight });
                    left += size.Width + horizontal;
                }
            }
            widest = (std::max)(widest, lineWidth);
            top += lineHeight;
            line.clear();
            lineWidth = 0.0f;
            lineHeight = 0.0f;
        };

        for (auto const& child : Children())
        {
            // A collapsed child occupies nothing, including the gap it would
            // otherwise open beside its neighbour.
            if (child.Visibility() == Microsoft::UI::Xaml::Visibility::Collapsed)
            {
                continue;
            }

            auto const size = child.DesiredSize();
            if (!line.empty() && lineWidth + horizontal + size.Width > limit)
            {
                flush();
                top += vertical;
            }

            if (!line.empty())
            {
                lineWidth += horizontal;
            }
            lineWidth += size.Width;
            lineHeight = (std::max)(lineHeight, size.Height);
            line.push_back(child);
        }

        if (!line.empty())
        {
            flush();
        }
        return Windows::Foundation::Size{ widest, top };
    }
}
