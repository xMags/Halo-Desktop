#pragma once

#include "SignInSpinner.g.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    // View behaviour only: the storyboard runs while the spinner is active,
    // loaded and the system's animation effects are on, and stops when it is
    // unloaded.
    struct SignInSpinner : SignInSpinnerT<SignInSpinner>
    {
        SignInSpinner() = default;

        [[nodiscard]] bool IsActive() const noexcept;
        void IsActive(bool value);

        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void Update();

        bool m_active{};
        bool m_loaded{};
        bool m_running{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SignInSpinner : SignInSpinnerT<SignInSpinner, implementation::SignInSpinner>
    {
    };
}
