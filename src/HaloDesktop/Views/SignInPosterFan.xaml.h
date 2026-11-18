#pragma once

#include "SignInPosterFan.g.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    // View behaviour only. The posters load once, on the first Loaded; the
    // entrance plays on each Loaded while the system's animation effects are
    // on and stops on Unloaded. Nothing here runs from the destructor.
    struct SignInPosterFan : SignInPosterFanT<SignInPosterFan>
    {
        SignInPosterFan() = default;

        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPosterFailed(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::ExceptionRoutedEventArgs const&);

    private:
        void LoadPosters();

        bool m_postersLoaded{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SignInPosterFan : SignInPosterFanT<SignInPosterFan, implementation::SignInPosterFan>
    {
    };
}
