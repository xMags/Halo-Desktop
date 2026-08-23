#include "pch.h"
#include "Views/LoginPage.xaml.h"
#if __has_include("LoginPage.g.cpp")
#include "LoginPage.g.cpp"
#endif

#include "App.xaml.h"
#include "ViewModels/LoginViewModel.h"

namespace winrt::HaloDesktop::implementation
{
    LoginPage::LoginPage()
        : m_viewModel(winrt::make<LoginViewModel>(App::Services()))
    {
    }

    winrt::HaloDesktop::LoginViewModel LoginPage::ViewModel() const { return m_viewModel; }
    void LoginPage::OnSignInClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_viewModel.SignIn(); }
    void LoginPage::OnMeridianClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_viewModel.ContinueWithMeridian(); }
    void LoginPage::OnDifferentServerClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_viewModel.UseDifferentServer(); }
}
