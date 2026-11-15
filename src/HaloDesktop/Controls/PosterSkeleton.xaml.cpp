#include "pch.h"
#include "Controls/PosterSkeleton.xaml.h"
#if __has_include("PosterSkeleton.g.cpp")
#include "PosterSkeleton.g.cpp"
#endif

#include "App.xaml.h"
#include "Shell/LayoutMetricsService.h"

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
        Width(current.PosterWidth);
        if (auto const frame = ArtFrame())
        {
            frame.Height(current.PosterArtHeight());
        }
    }
}
