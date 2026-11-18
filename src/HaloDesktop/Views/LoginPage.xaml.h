#pragma once

#include "LoginPage.g.h"

#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    // View behaviour only: layout that follows the window, focus, Enter to
    // submit, the password handed over and cleared, and the card's motion. The
    // steps live in LoginViewModel. Storyboards stop and the view model's
    // sign-in is superseded on Unloaded; nothing here runs from the destructor.
    struct LoginPage : LoginPageT<LoginPage>
    {
        LoginPage();

        [[nodiscard]] winrt::HaloDesktop::LoginViewModel ViewModel() const;

        void OnLoaded(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnStageSizeChanged(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::SizeChangedEventArgs const& args);
        void OnStartBrowserClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSubmitClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnFieldKeyDown(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
        void OnOpenLinkAgainClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCancelBrowserClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRetryClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnNoAccountClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnFormLinkClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnOpenCustomClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnDetailsClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnHelpClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void OnViewModelChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args);
        void FollowIntro();
        void FollowForm();
        void FollowSweep();
        void SubmitForm();
        winrt::fire_and_forget OpenSignInHelp();

        winrt::HaloDesktop::LoginViewModel m_viewModel{ nullptr };
        winrt::HaloDesktop::LoginViewModel::PropertyChanged_revoker m_viewModelChanged;
        bool m_introShown{ true };
        bool m_formShown{};
        bool m_serverFieldShown{};
        bool m_sweeping{};
        bool m_animate{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct LoginPage : LoginPageT<LoginPage, implementation::LoginPage> {};
}
