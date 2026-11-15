#include "pch.h"
#include "Controls/HaloMark.xaml.h"
#if __has_include("HaloMark.g.cpp")
#include "HaloMark.g.cpp"
#endif

#include <cmath>
#include <vector>

namespace
{
    using ::HaloDesktop::Controls::HaloMarkWeight;
    using ::HaloDesktop::Controls::MarkPolygon;

    // Computed once per weight: the cut is a few hundred thousand distance checks,
    // cheap once but not worth repeating for every mark on screen.
    std::vector<MarkPolygon> const& PiecesFor(HaloMarkWeight weight)
    {
        static auto const regularPieces = ::HaloDesktop::Controls::HaloMarkBarPieces(HaloMarkWeight::Regular);
        static auto const smallPieces = ::HaloDesktop::Controls::HaloMarkBarPieces(HaloMarkWeight::Small);
        return weight == HaloMarkWeight::Small ? smallPieces : regularPieces;
    }

    // A fresh geometry per mark: a XAML geometry belongs to the one path it is set on.
    winrt::Microsoft::UI::Xaml::Media::PathGeometry BarGeometry(HaloMarkWeight weight)
    {
        winrt::Microsoft::UI::Xaml::Media::PathGeometry geometry;
        for (auto const& piece : PiecesFor(weight))
        {
            if (piece.size() < 3)
            {
                continue;
            }
            winrt::Microsoft::UI::Xaml::Media::PathFigure figure;
            figure.StartPoint({ static_cast<float>(piece.front().X), static_cast<float>(piece.front().Y) });
            figure.IsClosed(true);
            figure.IsFilled(true);
            winrt::Microsoft::UI::Xaml::Media::PolyLineSegment outline;
            for (std::size_t index = 1; index < piece.size(); ++index)
            {
                outline.Points().Append({ static_cast<float>(piece[index].X), static_cast<float>(piece[index].Y) });
            }
            figure.Segments().Append(outline);
            geometry.Figures().Append(figure);
        }
        return geometry;
    }
}

namespace winrt::HaloDesktop::implementation
{
    void HaloMark::InitializeComponent()
    {
        HaloMarkT::InitializeComponent();
        m_initialized = true;
        ApplySize();
    }

    double HaloMark::Size() const noexcept
    {
        return m_size;
    }

    void HaloMark::Size(double value)
    {
        if (!std::isfinite(value) || value <= 0.0)
        {
            return;
        }
        m_size = value;
        ApplySize();
    }

    void HaloMark::ApplySize()
    {
        if (!m_initialized)
        {
            return;
        }
        MarkBox().Width(m_size);
        MarkBox().Height(m_size);
        auto const weight = ::HaloDesktop::Controls::HaloMarkWeightFor(m_size);
        if (m_weightApplied && weight == m_weight)
        {
            return;
        }
        ApplyWeight(weight);
    }

    void HaloMark::ApplyWeight(::HaloDesktop::Controls::HaloMarkWeight weight)
    {
        auto const ringWidth = ::HaloDesktop::Controls::HaloMarkRingWidth(weight);
        BackRing().StrokeThickness(ringWidth);
        FrontRing().StrokeThickness(ringWidth);
        Bars().Data(BarGeometry(weight));
        m_weight = weight;
        m_weightApplied = true;
    }
}
