#pragma once

#include "ConnectViewModel.g.h"

#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"

#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    struct ConnectViewModel : ConnectViewModelT<ConnectViewModel>
    {
        explicit ConnectViewModel(::HaloDesktop::Services::AppServices const& services);

        [[nodiscard]] winrt::hstring ServerUrl() const;
        void ServerUrl(winrt::hstring const& value);
        [[nodiscard]] bool IsTesting() const noexcept;
        [[nodiscard]] bool IsReached() const noexcept;
        [[nodiscard]] bool CanTest() const noexcept;
        [[nodiscard]] bool CanContinue() const noexcept;
        [[nodiscard]] winrt::hstring StatusText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility StatusVisibility() const noexcept;

        Windows::Foundation::IAsyncAction TestServerAsync();
        void Continue();

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void Raise(wchar_t const* propertyName);
        void RaiseState();

        std::shared_ptr<::HaloDesktop::Services::ISessionService> m_session;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::hstring m_serverUrl;
        winrt::hstring m_statusText;
        std::uint64_t m_requestVersion{};
        bool m_isTesting{};
        bool m_isReached{};
        bool m_statusVisible{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
