#include "pch.h"
#include "Controls/PressFeedback.h"

#include <chrono>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace
{
    constexpr std::chrono::milliseconds PressDuration{ 120 };

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

    void SetPressed(winrt::weak_ref<winrt::Microsoft::UI::Xaml::FrameworkElement> const& weak, bool pressed)
    {
        auto const target = weak.get();
        if (!target) return;
        try
        {
            // Released always goes back to full size, even if animations were
            // turned off mid-press.
            if (pressed && !AnimationsEnabled()) return;
            auto const width = static_cast<float>(target.ActualWidth());
            auto const height = static_cast<float>(target.ActualHeight());
            target.CenterPoint({ width / 2.0F, height / 2.0F, 0.0F });
            auto const scale = pressed ? ::HaloDesktop::Controls::PressedScale : 1.0F;
            target.Scale({ scale, scale, 1.0F });
        }
        catch (winrt::hresult_error const&)
        {
            // Decoration only: the click itself is unaffected.
        }
    }
}

namespace HaloDesktop::Controls
{
    void AttachPressFeedback(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& target,
        winrt::Microsoft::UI::Xaml::UIElement const& source)
    {
        if (!target || !source) return;
        winrt::Microsoft::UI::Xaml::Vector3Transition transition;
        transition.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(PressDuration));
        target.ScaleTransition(transition);

        auto const weak = winrt::make_weak(target);
        auto const press = winrt::Microsoft::UI::Xaml::Input::PointerEventHandler(
            [weak](winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
            {
                SetPressed(weak, true);
            });
        auto const release = winrt::Microsoft::UI::Xaml::Input::PointerEventHandler(
            [weak](winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
            {
                SetPressed(weak, false);
            });
        using winrt::Microsoft::UI::Xaml::UIElement;
        source.AddHandler(UIElement::PointerPressedEvent(), winrt::box_value(press), true);
        for (auto const& routed : {
                 UIElement::PointerReleasedEvent(),
                 UIElement::PointerCanceledEvent(),
                 UIElement::PointerCaptureLostEvent(),
                 UIElement::PointerExitedEvent() })
        {
            source.AddHandler(routed, winrt::box_value(release), true);
        }
    }
}
