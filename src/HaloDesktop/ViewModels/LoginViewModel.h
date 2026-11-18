#pragma once

#include "LoginViewModel.g.h"

#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include "ViewModels/LoginStepsPolicy.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    // UI-thread-only sign-in screen state. The steps and their copy come from
    // ViewModels/LoginStepsPolicy; this class runs the requests behind them.
    // Every sign-in is tagged with an attempt number, so a cancelled or
    // superseded attempt never changes the visible step. Asking the server
    // runs on its own count: its answer replaces the step only while that
    // step is still "discovering", so it never replaces a form opened
    // meanwhile.
    struct LoginViewModel : LoginViewModelT<LoginViewModel>
    {
        explicit LoginViewModel(::HaloDesktop::Services::AppServices const& services);

        [[nodiscard]] winrt::hstring Username() const;
        void Username(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring ServerAddress() const;
        void ServerAddress(winrt::hstring const& value);

        [[nodiscard]] Microsoft::UI::Xaml::Visibility IntroVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DiscoveringVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility BrowserVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FormVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ServerFieldVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility WaitingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility WaitingBrowserVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SignedInVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ResultVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility UnsupportedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility UnreachableVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PreparingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility NoAccountVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PasswordLinkVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DetailsVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ErrorVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AccountNameVisibility() const noexcept;
        [[nodiscard]] bool DiscoveringActive() const noexcept;
        [[nodiscard]] bool WaitingActive() const noexcept;
        [[nodiscard]] bool PreparingActive() const noexcept;
        [[nodiscard]] bool SignedInActive() const noexcept;
        [[nodiscard]] bool ReopenEnabled() const noexcept;

        [[nodiscard]] winrt::hstring ErrorText() const;
        [[nodiscard]] winrt::hstring MethodLabel() const;
        [[nodiscard]] winrt::hstring FormLinkText() const;
        [[nodiscard]] winrt::hstring WaitingTitle() const;
        [[nodiscard]] winrt::hstring WaitingBody() const;
        [[nodiscard]] winrt::hstring WaitingStatus() const;
        [[nodiscard]] winrt::hstring ResultTitle() const;
        [[nodiscard]] winrt::hstring ResultBody() const;
        [[nodiscard]] winrt::hstring UnsupportedBody() const;
        [[nodiscard]] winrt::hstring Detail() const;
        [[nodiscard]] winrt::hstring DetailsGlyph() const;
        [[nodiscard]] winrt::hstring AccountName() const;
        [[nodiscard]] winrt::hstring AccountLabel() const;
        [[nodiscard]] winrt::hstring AvatarInitial() const;
        [[nodiscard]] winrt::hstring SignedInStatus() const;
        [[nodiscard]] winrt::hstring NoAccountLabel() const;
        // Why Halo could not start on this PC, on the steps that offer it.
        [[nodiscard]] winrt::hstring DeviceErrorText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DeviceErrorVisibility() const noexcept;
        [[nodiscard]] bool ServerFieldFirst() const noexcept;

        void Retry();
        void StartBrowserSignIn();
        void OpenLinkAgain();
        void CancelBrowserSignIn();
        void Submit(winrt::hstring const& password);
        void FormLink();
        void OpenCustom();
        void ToggleDetails();
        void ContinueWithoutAccount();
        void Close();

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        using Step = ::HaloDesktop::ViewModels::LoginStep;
        using StepKind = ::HaloDesktop::ViewModels::LoginStepKind;

        winrt::fire_and_forget DiscoverAsync();
        winrt::fire_and_forget LoadLastServerAsync();
        winrt::fire_and_forget SubmitLocalAsync(winrt::hstring password);
        winrt::fire_and_forget SubmitCustomAsync(
            winrt::hstring password,
            ::HaloDesktop::ViewModels::RestingStep back);
        winrt::fire_and_forget StartBrowserAsync(::HaloDesktop::ViewModels::BrowserMethod method);
        winrt::fire_and_forget FinishAsync(std::uint64_t attempt);
        winrt::fire_and_forget ContinueOnDeviceAsync();
        void OnAuthUrl(std::uint64_t attempt, winrt::hstring const& authUrl);

        [[nodiscard]] bool Is(StepKind kind) const noexcept;
        // The screen is open over Halo without an account (the reference's
        // `signingIn`): its way out is "Back to Halo".
        [[nodiscard]] bool OpenedFromDevice() const noexcept;
        void SetStep(Step step);
        void Raise(wchar_t const* propertyName);

        std::shared_ptr<::HaloDesktop::Services::ISessionService> m_session;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        std::shared_ptr<::HaloDesktop::Services::Device::DeviceDataMove> m_deviceMove;
        Step m_step;
        // Halo could not start on this PC (or read its profile at launch).
        std::wstring m_deviceError;
        // What asking the server found, once it has answered.
        std::optional<::HaloDesktop::ViewModels::RestingStep> m_discovered;
        winrt::hstring m_username;
        winrt::hstring m_serverAddress;
        bool m_serverAddressEdited{};
        // The sign-in page the browser was sent to, for "Open the link again".
        winrt::hstring m_authUrl;
        bool m_detailsOpen{};
        bool m_closed{};
        std::uint64_t m_attempt{};
        std::uint64_t m_discovery{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
