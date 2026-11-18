#pragma once

#include "ViewModels/DownloadsPresentationPolicy.h"

#include <span>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Views
{
    // The Downloads page's throughput chart: a fixed row of bars, one per slot,
    // whose heights ease to each new sample (500 ms, the reference's
    // cubic-bezier(.4, 0, .2, 1)), so the row flows instead of jumping. Each bar
    // keeps its element across samples, as the reference keys bars by slot.
    // UI thread only; owned by the page whose host grid it fills.
    class DownloadsChartPresenter final
    {
    public:
        DownloadsChartPresenter() = default;
        DownloadsChartPresenter(DownloadsChartPresenter const&) = delete;
        DownloadsChartPresenter& operator=(DownloadsChartPresenter const&) = delete;

        // Builds the bars into `host` once. Their fill follows `brushSource`'s
        // Background, which the page sets from a theme resource, so a theme
        // switch repaints them without this class knowing about themes.
        void Attach(
            winrt::Microsoft::UI::Xaml::Controls::Grid const& host,
            winrt::Microsoft::UI::Xaml::FrameworkElement const& brushSource);
        void Apply(std::span<ViewModels::DownloadsPresentation::ChartBar const> bars);

    private:
        struct Bar final
        {
            winrt::Microsoft::UI::Xaml::Controls::Border Element{ nullptr };
            winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard Motion{ nullptr };
        };

        void Ease(Bar& bar, double height, bool animate);

        winrt::Microsoft::UI::Xaml::Controls::Grid m_host{ nullptr };
        std::vector<Bar> m_bars;
        bool m_applied{};
    };
}
