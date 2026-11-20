#include "pch.h"
#include "Controls/SearchBoxFlight.h"

#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

namespace
{
    namespace Animation = winrt::Microsoft::UI::Xaml::Media::Animation;

    constexpr wchar_t FlightKey[] = L"HaloSearchBoxFlight";
}

namespace HaloDesktop::Controls
{
    void PrepareSearchBoxFlight(winrt::Microsoft::UI::Xaml::UIElement const& source) noexcept
    {
        if (!source) return;
        try
        {
            Animation::ConnectedAnimationService::GetForCurrentView().PrepareToAnimate(FlightKey, source);
        }
        catch (...)
        {
            // No flight; Search still opens.
        }
    }

    bool StartSearchBoxFlight(winrt::Microsoft::UI::Xaml::UIElement const& destination) noexcept
    {
        if (!destination) return false;
        try
        {
            auto const animation = Animation::ConnectedAnimationService::GetForCurrentView().GetAnimation(FlightKey);
            if (!animation) return false;
            // A plain glide: the gravity default dips and casts a shadow, which
            // suits a picture lifting out of a grid, not a box growing in place.
            animation.Configuration(Animation::BasicConnectedAnimationConfiguration{});
            return animation.TryStart(destination);
        }
        catch (...)
        {
            return false;
        }
    }
}
