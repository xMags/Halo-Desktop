#pragma once

#include "LoginViewModel.g.h"

#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"

#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    // UI-thread-only login state machine. Discovery selects the local form or
    // browser flow; a request version prevents stale completions from changing
    // the visible step.
    struct LoginViewModel : LoginViewModelT<LoginViewModel>
    {
        explicit LoginViewModel(::HaloDesktop::Services::AppServices const& services);

        [[nodiscard]] winrt::hstring ServerHost() const;
        [[nodiscard]] winrt::hstring DisplayName() const;
        [[nodiscard]] winrt::hstring Username() const;
        void Username(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring LocalErrorText() const;
        [[nodiscard]] winrt::hstring WaitingTitle() const;
        [[nodiscard]] winrt::hstring WaitingBody() const;
        [[nodiscard]] winrt::hstring WaitingStatus() const;

        [[nodiscard]] Microsoft::UI::Xaml::Visibility DiscoveringVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LocalVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility OidcVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility WaitingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility WaitingCancelVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SignedInVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DeclinedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ExpiredVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility UnreachableVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DetailsVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LocalErrorVisibility() const noexcept;
        [[nodiscard]] winrt::hstring DetailsGlyph() const;

        void RetryDiscovery();
        void StartLocalSignIn(winrt::hstring const& password);
        void StartSignIn();
        void Reopen();
        void Cancel();
        void ToggleDetails();

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        enum class Step
        {
            Discovering,
            Local,
            Oidc,
            Waiting,
            SignedIn,
            Declined,
            Expired,
            Unreachable,
        };

        winrt::Windows::Foundation::IAsyncAction RunDiscoveryAsync();
        winrt::Windows::Foundation::IAsyncAction RunLocalSignInAsync(winrt::hstring password);
        winrt::Windows::Foundation::IAsyncAction RunBrowserSignInAsync();
        winrt::Windows::Foundation::IAsyncAction FinishAsync(std::uint32_t version);

        void SetStep(Step step);
        void SetLocalError(winrt::hstring value);
        void RaiseSteps();
        void RaiseWaitingCopy();
        void Raise(wchar_t const* propertyName);

        std::shared_ptr<::HaloDesktop::Services::ISessionService> m_session;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::hstring m_serverHost;
        winrt::hstring m_username;
        winrt::hstring m_localError;
        Step m_step{ Step::Discovering };
        bool m_detailsOpen{};
        bool m_waitingForLocal{};
        std::uint32_t m_requestVersion{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
