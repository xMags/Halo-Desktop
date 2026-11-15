#include "pch.h"
#include "Controls/SkeletonPulse.h"

#include <chrono>
#include <stdexcept>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace
{
    constexpr float PulseLow = 0.45F;
    constexpr float PulseHigh = 0.95F;
    // `prefers-reduced-motion`: no pulse, a steady 0.7.
    constexpr float PulseStill = 0.7F;
    // One way; the animation runs back the same way, as `alternate` does.
    constexpr std::chrono::milliseconds PulseLeg{ 1400 };

    bool AnimationsEnabled()
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

namespace HaloDesktop::Controls
{
    SkeletonPulse::SkeletonPulse(winrt::Microsoft::UI::Composition::Compositor const& compositor)
    {
        if (!compositor)
        {
            throw std::invalid_argument{ "The skeleton pulse needs a compositor." };
        }
        m_clock = compositor.CreatePropertySet();
        if (!AnimationsEnabled())
        {
            m_clock.InsertScalar(L"Opacity", PulseStill);
        }
        else
        {
            m_clock.InsertScalar(L"Opacity", PulseLow);
            // CSS `ease-in-out`.
            auto const easing = compositor.CreateCubicBezierEasingFunction({ 0.42F, 0.0F }, { 0.58F, 1.0F });
            m_pulse = compositor.CreateScalarKeyFrameAnimation();
            m_pulse.InsertKeyFrame(0.0F, PulseLow);
            m_pulse.InsertKeyFrame(1.0F, PulseHigh, easing);
            m_pulse.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(PulseLeg));
            m_pulse.IterationBehavior(winrt::Microsoft::UI::Composition::AnimationIterationBehavior::Forever);
            m_pulse.Direction(winrt::Microsoft::UI::Composition::AnimationDirection::Alternate);
        }

        m_follow = compositor.CreateExpressionAnimation(L"clock.Opacity");
        m_follow.SetReferenceParameter(L"clock", m_clock);
    }

    void SkeletonPulse::SetActive(bool active)
    {
        if (!m_pulse || active == m_active)
        {
            return;
        }
        m_active = active;
        if (active)
        {
            m_clock.StartAnimation(L"Opacity", m_pulse);
            return;
        }
        m_clock.StopAnimation(L"Opacity");
    }

    void SkeletonPulse::Attach(winrt::Microsoft::UI::Xaml::UIElement const& element) const
    {
        if (!element)
        {
            return;
        }
        winrt::Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(element)
            .StartAnimation(L"Opacity", m_follow);
    }
}
