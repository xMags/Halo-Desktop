#pragma once

#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Views
{
    // The system's "Animation effects" setting, the reference's
    // prefers-reduced-motion. Off (no animation) when it cannot be read.
    [[nodiscard]] bool SignInAnimationsEnabled() noexcept;

    // Starts or stops the storyboard keyed `key` in an element's Resources.
    // Decorative: a storyboard that is missing or refuses leaves the element
    // where its markup puts it, which is always its finished state.
    void BeginSignInStoryboard(winrt::Microsoft::UI::Xaml::ResourceDictionary const& resources, wchar_t const* key) noexcept;
    void StopSignInStoryboard(winrt::Microsoft::UI::Xaml::ResourceDictionary const& resources, wchar_t const* key) noexcept;
}
