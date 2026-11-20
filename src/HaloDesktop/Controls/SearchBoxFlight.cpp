#include "pch.h"
#include "Controls/SearchBoxFlight.h"

#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

namespace
{
    namespace Animation = winrt::Microsoft::UI::Xaml::Media::Animation;

    constexpr wchar_t FlightKey[] = L"HaloSearchBoxFlight";
    constexpr wchar_t ReturnKey[] = L"HaloSearchBoxReturn";

    void Prepare(wchar_t const* key, winrt::Microsoft::UI::Xaml::UIElement const& source) noexcept
    {
        if (!source) return;
        try
        {
            Animation::ConnectedAnimationService::GetForCurrentView().PrepareToAnimate(key, source);
        }
        catch (...)
        {
            // No flight; the navigation still happens.
        }
    }

    // `configuration` is made by the caller inside its own try, since making
    // one can throw too.
    bool Start(
        wchar_t const* key,
        Animation::ConnectedAnimationConfiguration const& configuration,
        winrt::Microsoft::UI::Xaml::UIElement const& destination) noexcept
    {
        if (!destination) return false;
        try
        {
            auto const animation = Animation::ConnectedAnimationService::GetForCurrentView().GetAnimation(key);
            if (!animation) return false;
            animation.Configuration(configuration);
            return animation.TryStart(destination);
        }
        catch (...)
        {
            return false;
        }
    }
}

namespace HaloDesktop::Controls
{
    void PrepareSearchBoxFlight(winrt::Microsoft::UI::Xaml::UIElement const& source) noexcept
    {
        Prepare(FlightKey, source);
    }

    bool StartSearchBoxFlight(winrt::Microsoft::UI::Xaml::UIElement const& destination) noexcept
    {
        try
        {
            // A plain glide: the gravity default dips and casts a shadow, which
            // suits a picture lifting out of a grid, not a box growing in place.
            return Start(FlightKey, Animation::BasicConnectedAnimationConfiguration{}, destination);
        }
        catch (...)
        {
            return false;
        }
    }

    void PrepareSearchBoxReturn(winrt::Microsoft::UI::Xaml::UIElement const& source) noexcept
    {
        Prepare(ReturnKey, source);
    }

    bool StartSearchBoxReturn(winrt::Microsoft::UI::Xaml::UIElement const& destination) noexcept
    {
        try
        {
            return Start(ReturnKey, Animation::DirectConnectedAnimationConfiguration{}, destination);
        }
        catch (...)
        {
            return false;
        }
    }
}
