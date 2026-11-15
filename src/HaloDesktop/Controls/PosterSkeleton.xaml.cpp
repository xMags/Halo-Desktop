#include "pch.h"
#include "Controls/PosterSkeleton.xaml.h"
#if __has_include("PosterSkeleton.g.cpp")
#include "PosterSkeleton.g.cpp"
#endif

#include "App.xaml.h"
#include "Shell/LayoutMetricsService.h"

#include <cmath>
#include <limits>

namespace
{
    // The 2:3 poster frame.
    constexpr double PosterAspect = 1.5;
}

namespace winrt::HaloDesktop::implementation
{
    // Sized here as well as on Loaded, for the reason PosterCard gives: a
    // skeleton measured at the markup's width and then resized would hand its
    // shelf a height that moves.
    PosterSkeleton::PosterSkeleton()
    {
        InitializeComponent();
        ApplyLayoutMetrics();
    }

    bool PosterSkeleton::FillsColumn() const noexcept { return m_fillsColumn; }
    void PosterSkeleton::FillsColumn(bool value)
    {
        if (m_fillsColumn == value) return;
        m_fillsColumn = value;
        if (value)
        {
            // The frame follows the width the column hands the skeleton, as a
            // poster card's art does, so the card that replaces it is that size.
            m_sizeChanged = SizeChanged(winrt::auto_revoke, [this](auto const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
            {
                ApplyColumnWidth(args.NewSize().Width);
            });
        }
        else
        {
            m_sizeChanged.revoke();
        }
        ApplyLayoutMetrics();
    }

    bool PosterSkeleton::ShowKindBadge() const noexcept { return m_showKindBadge; }
    void PosterSkeleton::ShowKindBadge(bool value)
    {
        m_showKindBadge = value;
        if (auto const bar = KindBar())
        {
            bar.Visibility(value ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed);
        }
    }

    // Follows the layout step as a poster card does, so the cards that replace
    // the skeletons are the same size.
    void PosterSkeleton::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_metricsToken != 0) return;
        auto const metrics = App::Services().LayoutMetrics;
        if (!metrics) return;
        m_metricsToken = metrics->AddChangedHandler([weak = get_weak()]()
        {
            if (auto const self = weak.get()) self->ApplyLayoutMetrics();
        });
        ApplyLayoutMetrics();
    }

    void PosterSkeleton::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_metricsToken == 0) return;
        if (auto const metrics = App::Services().LayoutMetrics) metrics->RemoveChangedHandler(m_metricsToken);
        m_metricsToken = 0;
    }

    void PosterSkeleton::ApplyLayoutMetrics()
    {
        auto const metrics = App::Services().LayoutMetrics;
        if (!metrics) return;
        auto const current = metrics->Current();
        if (m_fillsColumn)
        {
            // No width of its own: the column decides, and never below a poster.
            Width(std::numeric_limits<double>::quiet_NaN());
            MinWidth(current.PosterWidth);
            HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
            ApplyColumnWidth(ActualWidth() > 0.0 ? ActualWidth() : current.PosterWidth);
            return;
        }
        MinWidth(0.0);
        Width(current.PosterWidth);
        if (auto const frame = ArtFrame())
        {
            frame.Height(current.PosterArtHeight());
        }
    }

    void PosterSkeleton::ApplyColumnWidth(double width)
    {
        if (!std::isfinite(width) || width <= 0.0) return;
        auto const frame = ArtFrame();
        auto const height = std::round(width * PosterAspect);
        // Height follows width only, so this settles in one pass.
        if (frame && frame.Height() != height)
        {
            frame.Height(height);
        }
    }
}
