#pragma once

#include "LoginPage.g.h"

#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct LoginPage : LoginPageT<LoginPage>
    {
        LoginPage();

        [[nodiscard]] winrt::HaloDesktop::LoginViewModel ViewModel() const;
        void OnSignInClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnMeridianClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnDifferentServerClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        winrt::HaloDesktop::LoginViewModel m_viewModel{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct LoginPage : LoginPageT<LoginPage, implementation::LoginPage> {};
}
