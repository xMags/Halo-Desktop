#include "pch.h"
#include "Views/LoginPage.xaml.h"
#if __has_include("LoginPage.g.cpp")
#include "LoginPage.g.cpp"
#endif

#include "App.xaml.h"
#include "ViewModels/LoginViewModel.h"

#include <winrt/Windows.System.h>

namespace winrt::HaloDesktop::implementation
{
    LoginPage::LoginPage()
        : m_viewModel(winrt::make<LoginViewModel>(App::Services()))
    {
    }

    winrt::HaloDesktop::LoginViewModel LoginPage::ViewModel() const { return m_viewModel; }
    void LoginPage::OnLocalSignInClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { SubmitLocalCredentials(); }
    void LoginPage::OnPasswordKeyDown([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (!args.Handled() && args.Key() == winrt::Windows::System::VirtualKey::Enter)
        {
            args.Handled(true);
            SubmitLocalCredentials();
        }
    }
    void LoginPage::OnRetryClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_viewModel.RetryDiscovery(); }
    void LoginPage::OnContinueClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_viewModel.StartSignIn(); }
    void LoginPage::OnReopenClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_viewModel.Reopen(); }
    void LoginPage::OnCancelClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_viewModel.Cancel(); }
    void LoginPage::OnDetailsClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_viewModel.ToggleDetails(); }

    void LoginPage::SubmitLocalCredentials()
    {
        auto const password = PasswordInput().Password();
        PasswordInput().Password(L"");
        m_viewModel.StartLocalSignIn(password);
    }
}
