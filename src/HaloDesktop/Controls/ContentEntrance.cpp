#include "pch.h"
#include "Controls/ContentEntrance.h"

#include <chrono>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace
{
    namespace Animation = winrt::Microsoft::UI::Xaml::Media::Animation;

    constexpr std::chrono::milliseconds EntranceDuration{ 250 };
    constexpr double EntranceRise = 8.0;
    // Fluent's decelerate curve: cubic-bezier(0.1, 0.9, 0.2, 1).
    constexpr winrt::Windows::Foundation::Point DecelerateFirst{ 0.1F, 0.9F };
    constexpr winrt::Windows::Foundation::Point DecelerateSecond{ 0.2F, 1.0F };

    bool AnimationsEnabled()
    {
        try
        {
            return winrt::Windows::UI::ViewManagement::UISettings{}.AnimationsEnabled();
        }
        catch (winrt::hresult_error const&)
        {
            return false;
        }
    }

    Animation::DoubleAnimationUsingKeyFrames Ease(double from, double to)
    {
        Animation::DoubleAnimationUsingKeyFrames animation;
        Animation::DiscreteDoubleKeyFrame start;
        start.KeyTime(Animation::KeyTimeHelper::FromTimeSpan({}));
        start.Value(from);
        Animation::SplineDoubleKeyFrame end;
        end.KeyTime(Animation::KeyTimeHelper::FromTimeSpan(
            std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(EntranceDuration)));
        end.Value(to);
        Animation::KeySpline spline;
        spline.ControlPoint1(DecelerateFirst);
        spline.ControlPoint2(DecelerateSecond);
        end.KeySpline(spline);
        animation.KeyFrames().Append(start);
        animation.KeyFrames().Append(end);
        return animation;
    }
}

namespace HaloDesktop::Controls
{
    void PlayContentEntrance(winrt::Microsoft::UI::Xaml::UIElement const& element)
    {
        if (!element || !AnimationsEnabled()) return;
        try
        {
            auto translate = element.RenderTransform().try_as<winrt::Microsoft::UI::Xaml::Media::TranslateTransform>();
            if (!translate)
            {
                // Only an element with no transform of its own is given one.
                if (element.RenderTransform()) return;
                translate = winrt::Microsoft::UI::Xaml::Media::TranslateTransform{};
                element.RenderTransform(translate);
            }

            // A fresh storyboard each run, held only while it runs: kept on
            // the element, it and the element would hold each other, and a
            // recycled row would never be freed. A newer run takes over the
            // same two values, so a replay mid-run starts cleanly from 0.
            Animation::Storyboard storyboard;
            auto const fade = Ease(0.0, 1.0);
            Animation::Storyboard::SetTarget(fade, element);
            Animation::Storyboard::SetTargetProperty(fade, L"Opacity");
            auto const rise = Ease(EntranceRise, 0.0);
            Animation::Storyboard::SetTarget(rise, translate);
            Animation::Storyboard::SetTargetProperty(rise, L"Y");
            storyboard.Children().Append(fade);
            storyboard.Children().Append(rise);
            storyboard.Begin();
        }
        catch (winrt::hresult_error const&)
        {
            element.Opacity(1.0);
        }
    }
}
