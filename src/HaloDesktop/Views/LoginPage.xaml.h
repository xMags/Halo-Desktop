#pragma once

#include "LoginPage.g.h"

#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct LoginPage : LoginPageT<LoginPage>
    {
        LoginPage();

        [[nodiscard]] winrt::HaloDesktop::LoginViewModel ViewModel() const;
        void OnLocalSignInClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnPasswordKeyDown(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void OnRetryClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnContinueClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnReopenClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCancelClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnDetailsClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void SubmitLocalCredentials();
        winrt::HaloDesktop::LoginViewModel m_viewModel{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct LoginPage : LoginPageT<LoginPage, implementation::LoginPage> {};
}
