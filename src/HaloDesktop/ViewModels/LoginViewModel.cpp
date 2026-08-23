#include "pch.h"
#include "ViewModels/LoginViewModel.h"
#if __has_include("LoginViewModel.g.cpp")
#include "LoginViewModel.g.cpp"
#endif

#include "Services/NavigationService.h"
#include "ViewModels/ObservableHelper.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <winrt/Windows.Foundation.h>

namespace
{
    winrt::hstring UppercaseHost(winrt::hstring const& url)
    {
        if (url.empty())
        {
            return L"";
        }

        try
        {
            std::wstring host(winrt::Windows::Foundation::Uri{ url }.Host());
            std::transform(host.begin(), host.end(), host.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towupper(character));
            });
            return winrt::hstring(host);
        }
        catch (...)
        {
            return url;
        }
    }
}

namespace winrt::HaloDesktop::implementation
{
    LoginViewModel::LoginViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_session(services.Session),
          m_navigation(services.Navigation),
          m_serverHost(UppercaseHost(services.Session->ServerUrl()))
    {
    }

    winrt::hstring LoginViewModel::ServerHost() const { return m_serverHost; }
    winrt::hstring LoginViewModel::UserName() const { return m_userName; }
    void LoginViewModel::UserName(winrt::hstring const& value)
    {
        if (m_userName != value)
        {
            m_userName = value;
            Raise(L"UserName");
        }
    }
    winrt::hstring LoginViewModel::Password() const { return m_password; }
    void LoginViewModel::Password(winrt::hstring const& value)
    {
        if (m_password != value)
        {
            m_password = value;
            Raise(L"Password");
        }
    }
    winrt::hstring LoginViewModel::ErrorText() const { return m_errorText; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::ErrorVisibility() const noexcept
    {
        return m_errorVisible
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed;
    }

    void LoginViewModel::SignIn()
    {
        CompleteSignIn(m_userName, m_password);
    }

    void LoginViewModel::ContinueWithMeridian()
    {
        CompleteSignIn(L"debashis", L"");
    }

    void LoginViewModel::UseDifferentServer()
    {
        m_session->ClearServer();
        m_navigation->ShowOverlay(::HaloDesktop::Services::Page::Connect);
    }

    winrt::event_token LoginViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void LoginViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void LoginViewModel::CompleteSignIn(winrt::hstring const& user, winrt::hstring const& password)
    {
        if (!m_session->SignIn(user, password))
        {
            m_errorText = L"ENTER A USERNAME";
            m_errorVisible = true;
            Raise(L"ErrorText");
            Raise(L"ErrorVisibility");
            return;
        }

        m_password.clear();
        m_errorVisible = false;
        Raise(L"Password");
        Raise(L"ErrorVisibility");
        m_navigation->GoTo(::HaloDesktop::Services::Page::Home);
        m_navigation->CloseOverlay();
    }

    void LoginViewModel::Raise(wchar_t const* propertyName)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName);
    }
}
