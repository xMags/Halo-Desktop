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
    // Drives the browser sign-in screen. Exactly one step is visible at a time;
    // every visibility property below is derived from m_step.
    struct LoginViewModel : LoginViewModelT<LoginViewModel>
    {
        explicit LoginViewModel(::HaloDesktop::Services::AppServices const& services);

        [[nodiscard]] winrt::hstring ServerHost() const;
        [[nodiscard]] winrt::hstring DisplayName() const;

        [[nodiscard]] Microsoft::UI::Xaml::Visibility IdleVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility WaitingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SignedInVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DeclinedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ExpiredVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility UnreachableVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DetailsVisibility() const noexcept;
        [[nodiscard]] winrt::hstring DetailsGlyph() const;

        void StartSignIn();
        void Reopen();
        void Cancel();
        void ToggleDetails();

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        enum class Step
        {
            Idle,
            Waiting,
            SignedIn,
            Declined,
            Expired,
            Unreachable,
        };

        // Requests the browser round trip and renders whichever step it returns.
        // Guarded by m_requestVersion so a cancelled request cannot overwrite a
        // newer one when it finally completes.
        winrt::Windows::Foundation::IAsyncAction RunSignInAsync();

        // Holds the signed-in card on screen briefly, then lands on Home.
        winrt::Windows::Foundation::IAsyncAction FinishAsync(std::uint32_t version);

        void SetStep(Step step);
        void RaiseSteps();
        void Raise(wchar_t const* propertyName);

        std::shared_ptr<::HaloDesktop::Services::ISessionService> m_session;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::hstring m_serverHost;
        Step m_step{ Step::Idle };
        bool m_detailsOpen{};
        std::uint32_t m_requestVersion{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
