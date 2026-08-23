#pragma once

#include "SettingsViewModel.g.h"

#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"

#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    struct SettingsViewModel : SettingsViewModelT<SettingsViewModel>
    {
        explicit SettingsViewModel(::HaloDesktop::Services::AppServices const& services);

        [[nodiscard]] winrt::hstring ServerUrl() const;
        [[nodiscard]] winrt::hstring UserName() const;
        void Refresh();
        void SignOut();
        void SwitchServer();

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void Raise(wchar_t const* propertyName);

        std::shared_ptr<::HaloDesktop::Services::ISessionService> m_session;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::hstring m_serverUrl;
        winrt::hstring m_userName;
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
