#pragma once

#include "SignInBackdrop.g.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    // View behaviour only. The art loads once, on the first Loaded; the
    // crossfade runs while the control is loaded and the system's animation
    // effects are on. Nothing here runs from the destructor.
    struct SignInBackdrop : SignInBackdropT<SignInBackdrop>
    {
        SignInBackdrop() = default;

        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args);

    private:
        void LoadArt();

        bool m_artLoaded{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SignInBackdrop : SignInBackdropT<SignInBackdrop, implementation::SignInBackdrop>
    {
    };
}
