#include "pch.h"
#include "ViewModels/SettingsViewModel.h"
#if __has_include("SettingsViewModel.g.cpp")
#include "SettingsViewModel.g.cpp"
#endif

#include "Services/NavigationService.h"
#include "ViewModels/ObservableHelper.h"

namespace winrt::HaloDesktop::implementation
{
    SettingsViewModel::SettingsViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_session(services.Session), m_navigation(services.Navigation)
    {
        Refresh();
    }

    winrt::hstring SettingsViewModel::ServerUrl() const { return m_serverUrl; }
    winrt::hstring SettingsViewModel::UserName() const { return m_userName; }

    void SettingsViewModel::Refresh()
    {
        m_serverUrl = m_session->ServerUrl();
        m_userName = m_session->UserName();
        Raise(L"ServerUrl");
        Raise(L"UserName");
    }

    void SettingsViewModel::SignOut()
    {
        m_session->SignOut();
        Refresh();
        m_navigation->ShowOverlay(::HaloDesktop::Services::Page::Login);
    }

    void SettingsViewModel::SwitchServer()
    {
        m_session->ClearServer();
        Refresh();
        m_navigation->ShowOverlay(::HaloDesktop::Services::Page::Connect);
    }

    winrt::event_token SettingsViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void SettingsViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void SettingsViewModel::Raise(wchar_t const* propertyName)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName);
    }
}
