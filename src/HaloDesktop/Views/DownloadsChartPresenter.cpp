#include "pch.h"
#include "Views/DownloadsChartPresenter.h"

#include "Services/DownloadsLogicPolicy.h"

#include <chrono>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace
{
    // `.dl-chart { height: 76px }`; the host grid is drawn at this height.
    constexpr double ChartHeight = 76.0;
    constexpr std::chrono::milliseconds EaseDuration{ 500 };

    bool AnimationsEnabled() noexcept
    {
        try
        {
            return winrt::Windows::UI::ViewManagement::UISettings{}.AnimationsEnabled();
        }
        catch (...)
        {
            return true;
        }
    }
}

namespace HaloDesktop::Views
{
    void DownloadsChartPresenter::Attach(
        winrt::Microsoft::UI::Xaml::Controls::Grid const& host,
        winrt::Microsoft::UI::Xaml::FrameworkElement const& brushSource)
    {
        if (m_host || !host || !brushSource) return;
        m_host = host;
        auto const slots = Services::DownloadsLogic::ThroughputSlots;
        m_bars.reserve(slots);
        for (std::size_t slot = 0; slot < slots; ++slot)
        {
            winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition column;
            column.Width(winrt::Microsoft::UI::Xaml::GridLengthHelper::FromValueAndType(1.0, winrt::Microsoft::UI::Xaml::GridUnitType::Star));
            host.ColumnDefinitions().Append(column);

            winrt::Microsoft::UI::Xaml::Controls::Border bar;
            bar.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Bottom);
            bar.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{ 3.0, 3.0, 1.0, 1.0 });
            bar.Height(ViewModels::DownloadsPresentation::ChartFloor * ChartHeight);
            bar.IsHitTestVisible(false);
            winrt::Microsoft::UI::Xaml::Data::Binding fill;
            fill.Source(brushSource);
            fill.Path(winrt::Microsoft::UI::Xaml::PropertyPath{ L"Background" });
            bar.SetBinding(winrt::Microsoft::UI::Xaml::Controls::Border::BackgroundProperty(), fill);
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(bar, static_cast<std::int32_t>(slot));
            host.Children().Append(bar);
            m_bars.push_back(Bar{ .Element = bar });
        }
    }

    void DownloadsChartPresenter::Apply(std::span<ViewModels::DownloadsPresentation::ChartBar const> bars)
    {
        if (!m_host) return;
        // The first reading is drawn where it stands rather than grown from the floor.
        auto const animate = m_applied && AnimationsEnabled();
        for (std::size_t index = 0; index < m_bars.size() && index < bars.size(); ++index)
        {
            auto& bar = m_bars[index];
            try
            {
                bar.Element.Opacity(bars[index].Opacity);
                Ease(bar, bars[index].Fraction * ChartHeight, animate);
            }
            catch (winrt::hresult_error const&)
            {
                // Decoration only: a bar that cannot ease is drawn at its height.
                try { bar.Element.Height(bars[index].Fraction * ChartHeight); } catch (...) {}
            }
        }
        m_applied = true;
    }

    void DownloadsChartPresenter::Ease(Bar& bar, double height, bool animate)
    {
        namespace Animation = winrt::Microsoft::UI::Xaml::Media::Animation;
        // The bar starts from wherever the last ease left it: its drawn height
        // becomes the base before the old storyboard lets go of it.
        auto const current = bar.Element.ActualHeight();
        if (bar.Motion)
        {
            if (current > 0.0) bar.Element.Height(current);
            bar.Motion.Stop();
            bar.Motion = nullptr;
        }
        if (!animate)
        {
            bar.Element.Height(height);
            return;
        }
        Animation::SplineDoubleKeyFrame frame;
        frame.KeyTime(Animation::KeyTimeHelper::FromTimeSpan(
            std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(EaseDuration)));
        frame.Value(height);
        Animation::KeySpline spline;
        spline.ControlPoint1({ 0.4F, 0.0F });
        spline.ControlPoint2({ 0.2F, 1.0F });
        frame.KeySpline(spline);
        Animation::DoubleAnimationUsingKeyFrames animation;
        animation.EnableDependentAnimation(true);
        animation.KeyFrames().Append(frame);
        Animation::Storyboard::SetTarget(animation, bar.Element);
        Animation::Storyboard::SetTargetProperty(animation, L"Height");
        Animation::Storyboard storyboard;
        storyboard.Children().Append(animation);
        bar.Motion = storyboard;
        storyboard.Begin();
    }
}
