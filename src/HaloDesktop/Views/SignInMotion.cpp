#include "pch.h"
#include "Views/SignInMotion.h"

#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace
{
    winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard Find(
        winrt::Microsoft::UI::Xaml::ResourceDictionary const& resources,
        wchar_t const* key)
    {
        if (!resources)
        {
            return nullptr;
        }
        auto const boxedKey = winrt::box_value(winrt::hstring{ key });
        if (!resources.HasKey(boxedKey))
        {
            return nullptr;
        }
        return resources.Lookup(boxedKey).try_as<winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard>();
    }
}

namespace HaloDesktop::Views
{
    bool SignInAnimationsEnabled() noexcept
    {
        try
        {
            return winrt::Windows::UI::ViewManagement::UISettings{}.AnimationsEnabled();
        }
        catch (...)
        {
            return false;
        }
    }

    void BeginSignInStoryboard(winrt::Microsoft::UI::Xaml::ResourceDictionary const& resources, wchar_t const* key) noexcept
    {
        try
        {
            if (auto const storyboard = Find(resources, key))
            {
                storyboard.Stop();
                storyboard.Begin();
            }
        }
        catch (...)
        {
        }
    }

    void StopSignInStoryboard(winrt::Microsoft::UI::Xaml::ResourceDictionary const& resources, wchar_t const* key) noexcept
    {
        try
        {
            if (auto const storyboard = Find(resources, key))
            {
                storyboard.Stop();
            }
        }
        catch (...)
        {
        }
    }
}
