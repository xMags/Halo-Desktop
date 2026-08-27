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
    namespace
    {
        constexpr wchar_t const* SignInHelpUrl = L"https://github.com/xMags/Halo-Desktop/issues";
    }

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
    void LoginPage::OnHelpClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { OpenSignInHelp(); }

    winrt::fire_and_forget LoginPage::OpenSignInHelp()
    {
        auto lifetime = get_strong();
        auto launched = false;
        try
        {
            launched = co_await winrt::Windows::System::Launcher::LaunchUriAsync(
                winrt::Windows::Foundation::Uri{ SignInHelpUrl });
        }
        catch (...)
        {
        }
        if (launched)
        {
            co_return;
        }

        try
        {
            Microsoft::UI::Xaml::Controls::ContentDialog dialog;
            dialog.XamlRoot(XamlRoot());
            dialog.Title(winrt::box_value(L"Help could not be opened"));
            dialog.Content(winrt::box_value(L"Open the Halo Desktop issues page in a browser and try again."));
            dialog.CloseButtonText(L"Close");
            co_await dialog.ShowAsync();
        }
        catch (...)
        {
        }
    }

    void LoginPage::SubmitLocalCredentials()
    {
        auto const password = PasswordInput().Password();
        PasswordInput().Password(L"");
        m_viewModel.StartLocalSignIn(password);
    }
}
