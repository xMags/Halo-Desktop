#include "pch.h"
#include "Views/LoginPage.xaml.h"
#if __has_include("LoginPage.g.cpp")
#include "LoginPage.g.cpp"
#endif

#include "App.xaml.h"
#include "ViewModels/LoginViewModel.h"
#include "Views/PageDialog.h"
#include "Views/SignInMotion.h"

#include <limits>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.System.h>

namespace
{
    // This app's own issue tracker, beside the releases Settings links to.
    constexpr wchar_t const* SignInHelpUrl = L"https://github.com/xMags/Halo-Desktop/issues";

    // `@container (max-width: 1100px)`: below this the fan is left out and the
    // card stands alone, centred.
    constexpr double FanMinimumStageWidth = 1100.0;
    constexpr double DeckWidth = 1060.0;

    constexpr wchar_t const* CardRiseStoryboard = L"CardRiseStoryboard";
    constexpr wchar_t const* IntroRiseStoryboard = L"IntroRiseStoryboard";
    constexpr wchar_t const* SweepStoryboard = L"SweepStoryboard";

    bool IsVisible(winrt::Microsoft::UI::Xaml::Visibility visibility) noexcept
    {
        return visibility == winrt::Microsoft::UI::Xaml::Visibility::Visible;
    }
}

namespace winrt::HaloDesktop::implementation
{
    LoginPage::LoginPage()
        : m_viewModel(winrt::make<LoginViewModel>(App::Services()))
    {
    }

    winrt::HaloDesktop::LoginViewModel LoginPage::ViewModel() const { return m_viewModel; }

    void LoginPage::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_animate = ::HaloDesktop::Views::SignInAnimationsEnabled();
        m_viewModelChanged = m_viewModel.PropertyChanged(
            winrt::auto_revoke,
            { get_weak(), &LoginPage::OnViewModelChanged });
        m_introShown = IsVisible(m_viewModel.IntroVisibility());
        m_formShown = false;
        m_serverFieldShown = false;
        if (m_animate)
        {
            ::HaloDesktop::Views::BeginSignInStoryboard(Resources(), CardRiseStoryboard);
        }
        else
        {
            // With motion off the sweep stands still where it can be seen.
            try
            {
                SweepShift().X(0.0);
            }
            catch (winrt::hresult_error const&)
            {
            }
        }
        FollowForm();
        FollowSweep();
    }

    void LoginPage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_viewModelChanged.revoke();
        ::HaloDesktop::Views::StopSignInStoryboard(Resources(), CardRiseStoryboard);
        ::HaloDesktop::Views::StopSignInStoryboard(Resources(), IntroRiseStoryboard);
        ::HaloDesktop::Views::StopSignInStoryboard(Resources(), SweepStoryboard);
        m_sweeping = false;
        try
        {
            PasswordInput().Password(L"");
        }
        catch (winrt::hresult_error const&)
        {
        }
        m_viewModel.Close();
    }

    void LoginPage::OnStageSizeChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        try
        {
            auto const size = args.NewSize();
            // The content is at least the stage, so the deck centres in it;
            // a smaller stage scrolls instead of clipping the card.
            StageContent().MinWidth(size.Width);
            StageContent().MinHeight(size.Height);
            auto const roomForFan = size.Width >= FanMinimumStageWidth;
            Fan().Visibility(roomForFan
                ? Microsoft::UI::Xaml::Visibility::Visible
                : Microsoft::UI::Xaml::Visibility::Collapsed);
            Deck().Width(roomForFan ? DeckWidth : std::numeric_limits<double>::quiet_NaN());
            Card().HorizontalAlignment(roomForFan
                ? Microsoft::UI::Xaml::HorizontalAlignment::Left
                : Microsoft::UI::Xaml::HorizontalAlignment::Center);
        }
        catch (winrt::hresult_error const&)
        {
            // The layout stays as it was.
        }
    }

    void LoginPage::OnStartBrowserClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.StartBrowserSignIn(); }
    void LoginPage::OnSubmitClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { SubmitForm(); }
    void LoginPage::OnOpenLinkAgainClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenLinkAgain(); }
    void LoginPage::OnCancelBrowserClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.CancelBrowserSignIn(); }
    void LoginPage::OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Retry(); }
    void LoginPage::OnNoAccountClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.ContinueWithoutAccount(); }
    void LoginPage::OnFormLinkClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.FormLink(); }
    void LoginPage::OnOpenCustomClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenCustom(); }
    void LoginPage::OnDetailsClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.ToggleDetails(); }
    void LoginPage::OnHelpClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { OpenSignInHelp(); }

    void LoginPage::OnFieldKeyDown(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Handled() || args.Key() != winrt::Windows::System::VirtualKey::Enter)
        {
            return;
        }
        args.Handled(true);
        SubmitForm();
    }

    void LoginPage::SubmitForm()
    {
        // The password lives only in the box until it is handed over, and the
        // box is emptied as it is.
        auto const password = PasswordInput().Password();
        PasswordInput().Password(L"");
        m_viewModel.Submit(password);
    }

    void LoginPage::OnViewModelChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args)
    {
        auto const name = args.PropertyName();
        if (name == L"IntroVisibility")
        {
            FollowIntro();
        }
        else if (name == L"FormVisibility" || name == L"ServerFieldVisibility")
        {
            FollowForm();
        }
        else if (name == L"SignedInVisibility")
        {
            FollowSweep();
        }
    }

    void LoginPage::FollowIntro()
    {
        auto const shown = IsVisible(m_viewModel.IntroVisibility());
        if (shown && !m_introShown && m_animate)
        {
            ::HaloDesktop::Views::BeginSignInStoryboard(Resources(), IntroRiseStoryboard);
        }
        m_introShown = shown;
    }

    // A form that opens takes focus on its first empty field: the server
    // address when none is known yet, else the username.
    void LoginPage::FollowForm()
    {
        auto const formShown = IsVisible(m_viewModel.FormVisibility());
        auto const serverShown = IsVisible(m_viewModel.ServerFieldVisibility());
        auto const opened = formShown && (!m_formShown || serverShown != m_serverFieldShown);
        m_formShown = formShown;
        m_serverFieldShown = serverShown;
        if (!opened)
        {
            return;
        }
        auto const serverFirst = m_viewModel.ServerFieldFirst();
        // After layout, so the field being focused is the one now on screen.
        static_cast<void>(DispatcherQueue().TryEnqueue(
            Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [weak = get_weak(), serverFirst]()
            {
                auto const self = weak.get();
                if (!self || !self->m_formShown)
                {
                    return;
                }
                try
                {
                    if (serverFirst && self->m_serverFieldShown)
                    {
                        static_cast<void>(self->ServerInput().Focus(Microsoft::UI::Xaml::FocusState::Programmatic));
                    }
                    else
                    {
                        static_cast<void>(self->UsernameInput().Focus(Microsoft::UI::Xaml::FocusState::Programmatic));
                    }
                }
                catch (winrt::hresult_error const&)
                {
                }
            }));
    }

    void LoginPage::FollowSweep()
    {
        auto const sweep = m_animate && IsVisible(m_viewModel.SignedInVisibility());
        if (sweep == m_sweeping)
        {
            return;
        }
        m_sweeping = sweep;
        if (sweep)
        {
            ::HaloDesktop::Views::BeginSignInStoryboard(Resources(), SweepStoryboard);
        }
        else
        {
            ::HaloDesktop::Views::StopSignInStoryboard(Resources(), SweepStoryboard);
        }
    }

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
            auto dialog = ::HaloDesktop::Views::MakeDialog(
                XamlRoot(),
                ActualTheme(),
                L"Help could not be opened",
                L"Open the Halo Desktop issues page in a browser and try again.");
            dialog.CloseButtonText(L"Close");
            co_await dialog.ShowAsync();
        }
        catch (...)
        {
        }
    }
}
