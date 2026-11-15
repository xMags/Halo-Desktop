#include "Controls/HaloMarkGeometry.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

namespace
{
    namespace Metrics = ::HaloDesktop::Controls::HaloMarkMetrics;
    using ::HaloDesktop::Controls::MarkPoint;
    using ::HaloDesktop::Controls::MarkPolygon;

    // Samples along the front ring when measuring the band. The ring is about 60
    // units long, so neighbouring samples sit 0.015 units apart: far below a pixel
    // even when the mark is drawn at 256 pixels.
    constexpr int RingSamples = 4000;
    // Points along each edge of a bar piece. Spaced by angle around the rounded end
    // rather than evenly in x, so the steep sides of the rounded ends stay smooth.
    constexpr int EdgeSamples = 48;

    // Where the clearance band crosses one vertical line: the top and bottom of the
    // part of the band on that line. Empty when the band does not reach the line.
    struct BandSpan final
    {
        double Top{};
        double Bottom{};
    };

    class FrontBand final
    {
    public:
        explicit FrontBand(double halfWidth)
            : m_halfWidth(halfWidth)
        {
            m_samples.reserve(RingSamples + 1);
            for (int index = 0; index <= RingSamples; ++index)
            {
                m_samples.push_back(::HaloDesktop::Controls::HaloMarkFrontRingPoint(
                    static_cast<double>(index) / static_cast<double>(RingSamples)));
            }
        }

        // The band is every point within halfWidth of the ring, round caps included,
        // so its extent on a vertical line is the union of the discs around the
        // samples that reach that line.
        [[nodiscard]] std::optional<BandSpan> SpanAt(double x) const
        {
            std::optional<BandSpan> span;
            for (auto const& sample : m_samples)
            {
                auto const dx = x - sample.X;
                auto const reach = m_halfWidth * m_halfWidth - dx * dx;
                if (reach < 0.0)
                {
                    continue;
                }
                auto const half = std::sqrt(reach);
                if (!span)
                {
                    span = BandSpan{ sample.Y - half, sample.Y + half };
                    continue;
                }
                span->Top = (std::min)(span->Top, sample.Y - half);
                span->Bottom = (std::max)(span->Bottom, sample.Y + half);
            }
            return span;
        }

    private:
        double m_halfWidth;
        std::vector<MarkPoint> m_samples;
    };

    // Top and bottom outline of a bar with fully rounded ends at one x.
    struct BarOutline final
    {
        double Top{};
        double Bottom{};
    };

    BarOutline BarOutlineAt(double barLeft, double x) noexcept
    {
        auto const radius = Metrics::BarWidth / 2.0;
        auto const centreX = barLeft + radius;
        auto const dx = x - centreX;
        auto const rise = std::sqrt((std::max)(0.0, radius * radius - dx * dx));
        return BarOutline{
            Metrics::BarTop + radius - rise,
            Metrics::BarTop + Metrics::BarHeight - radius + rise,
        };
    }

    // The x positions an edge is sampled at, left to right.
    std::vector<double> EdgeColumns(double barLeft)
    {
        auto const radius = Metrics::BarWidth / 2.0;
        auto const centreX = barLeft + radius;
        std::vector<double> columns;
        columns.reserve(EdgeSamples + 1);
        for (int index = 0; index <= EdgeSamples; ++index)
        {
            auto const angle = std::numbers::pi * static_cast<double>(index) / static_cast<double>(EdgeSamples);
            columns.push_back(centreX - radius * std::cos(angle));
        }
        return columns;
    }

    // One bar split by the band: the piece above it and the piece below it. A column
    // the band does not cross keeps the whole bar in the top piece.
    std::pair<MarkPolygon, MarkPolygon> SplitBar(double barLeft, FrontBand const& band)
    {
        auto const columns = EdgeColumns(barLeft);
        MarkPolygon top;
        MarkPolygon bottom;
        top.reserve(columns.size() * 2);
        bottom.reserve(columns.size() * 2);

        std::vector<double> topPieceFloor;
        std::vector<double> bottomPieceCeiling;
        topPieceFloor.reserve(columns.size());
        bottomPieceCeiling.reserve(columns.size());
        for (auto const x : columns)
        {
            auto const outline = BarOutlineAt(barLeft, x);
            auto const span = band.SpanAt(x);
            auto const floor = span ? std::clamp(span->Top, outline.Top, outline.Bottom) : outline.Bottom;
            auto const ceiling = span ? std::clamp(span->Bottom, outline.Top, outline.Bottom) : outline.Bottom;
            topPieceFloor.push_back(floor);
            bottomPieceCeiling.push_back(ceiling);
        }

        // Each piece runs along its upper edge left to right, then back along its
        // lower edge, so both polygons wind the same way.
        for (std::size_t index = 0; index < columns.size(); ++index)
        {
            top.push_back({ columns[index], BarOutlineAt(barLeft, columns[index]).Top });
            bottom.push_back({ columns[index], bottomPieceCeiling[index] });
        }
        for (std::size_t index = columns.size(); index-- > 0;)
        {
            top.push_back({ columns[index], topPieceFloor[index] });
            bottom.push_back({ columns[index], BarOutlineAt(barLeft, columns[index]).Bottom });
        }
        return { std::move(top), std::move(bottom) };
    }
}

namespace HaloDesktop::Controls
{
    HaloMarkWeight HaloMarkWeightFor(double size) noexcept
    {
        return size <= HaloMarkMetrics::SmallMarkSize ? HaloMarkWeight::Small : HaloMarkWeight::Regular;
    }

    double HaloMarkRingWidth(HaloMarkWeight weight) noexcept
    {
        return weight == HaloMarkWeight::Small ? HaloMarkMetrics::SmallRingWidth : HaloMarkMetrics::RingWidth;
    }

    double HaloMarkRingClearance(HaloMarkWeight weight) noexcept
    {
        return weight == HaloMarkWeight::Small ? HaloMarkMetrics::SmallRingClearance : HaloMarkMetrics::RingClearance;
    }

    MarkPoint HaloMarkFrontRingPoint(double fraction) noexcept
    {
        // SVG's front half, "M5 34 A27 9.5 0 0 0 59 34": from the left end through
        // the bottom of the ellipse to the right end.
        auto const angle = std::numbers::pi * (1.0 - std::clamp(fraction, 0.0, 1.0));
        auto const dx = HaloMarkMetrics::RingRadiusX * std::cos(angle);
        auto const dy = HaloMarkMetrics::RingRadiusY * std::sin(angle);
        auto const tilt = HaloMarkMetrics::RingTiltDegrees * std::numbers::pi / 180.0;
        return MarkPoint{
            HaloMarkMetrics::RingCenterX + dx * std::cos(tilt) - dy * std::sin(tilt),
            HaloMarkMetrics::RingCenterY + dx * std::sin(tilt) + dy * std::cos(tilt),
        };
    }

    std::vector<MarkPolygon> HaloMarkBarPieces(HaloMarkWeight weight)
    {
        // The mask stroke is the ring's own width plus the clearance on top of it,
        // so the bars stand clearance / 2 off each edge of the visible ring.
        FrontBand const band{ (HaloMarkRingWidth(weight) + HaloMarkRingClearance(weight)) / 2.0 };
        auto [leftTop, leftBottom] = SplitBar(HaloMarkMetrics::LeftBarX, band);
        auto [rightTop, rightBottom] = SplitBar(HaloMarkMetrics::RightBarX, band);
        std::vector<MarkPolygon> pieces;
        pieces.reserve(4);
        pieces.push_back(std::move(leftTop));
        pieces.push_back(std::move(leftBottom));
        pieces.push_back(std::move(rightTop));
        pieces.push_back(std::move(rightBottom));
        return pieces;
    }
}
