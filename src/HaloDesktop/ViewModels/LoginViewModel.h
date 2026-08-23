#pragma once

#include "LoginViewModel.g.h"

#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"

#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    struct LoginViewModel : LoginViewModelT<LoginViewModel>
    {
        explicit LoginViewModel(::HaloDesktop::Services::AppServices const& services);

        [[nodiscard]] winrt::hstring ServerHost() const;
        [[nodiscard]] winrt::hstring UserName() const;
        void UserName(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring Password() const;
        void Password(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring ErrorText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ErrorVisibility() const noexcept;

        void SignIn();
        void ContinueWithMeridian();
        void UseDifferentServer();

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void CompleteSignIn(winrt::hstring const& user, winrt::hstring const& password);
        void Raise(wchar_t const* propertyName);

        std::shared_ptr<::HaloDesktop::Services::ISessionService> m_session;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::hstring m_serverHost;
        winrt::hstring m_userName;
        winrt::hstring m_password;
        winrt::hstring m_errorText;
        bool m_errorVisible{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
