#include "pch.h"
#include "ViewModels/LoginViewModel.h"
#if __has_include("LoginViewModel.g.cpp")
#include "LoginViewModel.g.cpp"
#endif

#include "Services/NavigationService.h"
#include "ViewModels/ObservableHelper.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <string>
#include <utility>

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
    constexpr std::chrono::milliseconds SignedInDwell{ 1200 };

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

    winrt::hstring CapitalizeFirst(winrt::hstring const& value)
    {
        std::wstring name{ value };
        if (!name.empty())
        {
            name.front() = static_cast<wchar_t>(std::towupper(name.front()));
        }
        return winrt::hstring{ name };
    }
}

namespace winrt::HaloDesktop::implementation
{
    LoginViewModel::LoginViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_session(services.Session),
          m_navigation(services.Navigation),
          m_serverHost(UppercaseHost(services.Session->ServerUrl()))
    {
        static_cast<void>(RunDiscoveryAsync());
    }

    winrt::hstring LoginViewModel::ServerHost() const { return m_serverHost; }
    winrt::hstring LoginViewModel::DisplayName() const { return CapitalizeFirst(m_session->UserName()); }
    winrt::hstring LoginViewModel::Username() const { return m_username; }
    void LoginViewModel::Username(winrt::hstring const& value)
    {
        if (m_username != value)
        {
            m_username = value;
            Raise(L"Username");
        }
    }
    winrt::hstring LoginViewModel::LocalErrorText() const { return m_localError; }
    winrt::hstring LoginViewModel::WaitingTitle() const { return m_waitingForLocal ? L"Signing in" : L"Check your browser"; }
    winrt::hstring LoginViewModel::WaitingBody() const
    {
        return m_waitingForLocal
            ? winrt::hstring{ L"Halo is securely checking your account with your server." }
            : winrt::hstring{ L"We opened a sign-in request in your default browser. Approve it there and this window carries on by itself." };
    }
    winrt::hstring LoginViewModel::WaitingStatus() const { return m_waitingForLocal ? L"CHECKING ACCOUNT" : L"WAITING FOR BROWSER"; }

    Microsoft::UI::Xaml::Visibility LoginViewModel::DiscoveringVisibility() const noexcept { return m_step == Step::Discovering ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::LocalVisibility() const noexcept { return m_step == Step::Local ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::OidcVisibility() const noexcept { return m_step == Step::Oidc ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::WaitingVisibility() const noexcept { return m_step == Step::Waiting ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::WaitingCancelVisibility() const noexcept { return m_step == Step::Waiting && !m_waitingForLocal ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::SignedInVisibility() const noexcept { return m_step == Step::SignedIn ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::DeclinedVisibility() const noexcept { return m_step == Step::Declined ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::ExpiredVisibility() const noexcept { return m_step == Step::Expired ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::UnreachableVisibility() const noexcept { return m_step == Step::Unreachable ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::DetailsVisibility() const noexcept { return m_detailsOpen ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::LocalErrorVisibility() const noexcept { return m_localError.empty() ? Collapsed : Visible; }
    winrt::hstring LoginViewModel::DetailsGlyph() const { return winrt::hstring{ m_detailsOpen ? L"\uE70D" : L"\uE76C" }; }

    void LoginViewModel::RetryDiscovery()
    {
        static_cast<void>(RunDiscoveryAsync());
    }

    void LoginViewModel::StartLocalSignIn(winrt::hstring const& password)
    {
        if (m_step != Step::Local)
        {
            return;
        }
        if (m_username.empty() || password.empty())
        {
            SetLocalError(L"Enter your username and password.");
            return;
        }
        static_cast<void>(RunLocalSignInAsync(password));
    }

    void LoginViewModel::StartSignIn()
    {
        if (m_step == Step::Unreachable)
        {
            RetryDiscovery();
            return;
        }
        if (m_step != Step::Oidc && m_step != Step::Declined && m_step != Step::Expired)
        {
            return;
        }
        static_cast<void>(RunBrowserSignInAsync());
    }

    void LoginViewModel::Reopen()
    {
        if (!m_waitingForLocal)
        {
            static_cast<void>(RunBrowserSignInAsync());
        }
    }

    void LoginViewModel::Cancel()
    {
        if (m_waitingForLocal)
        {
            return;
        }
        ++m_requestVersion;
        SetStep(Step::Oidc);
    }

    void LoginViewModel::ToggleDetails()
    {
        m_detailsOpen = !m_detailsOpen;
        Raise(L"DetailsVisibility");
        Raise(L"DetailsGlyph");
    }

    winrt::Windows::Foundation::IAsyncAction LoginViewModel::RunDiscoveryAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_requestVersion;
        SetLocalError(L"");
        SetStep(Step::Discovering);
        try
        {
            auto const mode = co_await m_session->DiscoverAuthenticationAsync();
            co_await uiContext;
            if (version != m_requestVersion)
            {
                co_return;
            }
            SetStep(mode == ::HaloDesktop::Services::AuthenticationMode::Local
                ? Step::Local
                : Step::Oidc);
        }
        catch (...)
        {
            if (version == m_requestVersion)
            {
                SetStep(Step::Unreachable);
            }
        }
    }

    winrt::Windows::Foundation::IAsyncAction LoginViewModel::RunLocalSignInAsync(winrt::hstring password)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_requestVersion;
        SetLocalError(L"");
        m_waitingForLocal = true;
        RaiseWaitingCopy();
        SetStep(Step::Waiting);

        auto const outcome = co_await m_session->SignInLocalAsync(m_username, std::move(password));
        co_await uiContext;
        if (version != m_requestVersion)
        {
            co_return;
        }

        switch (outcome)
        {
        case winrt::HaloDesktop::SignInOutcome::Succeeded:
            Raise(L"DisplayName");
            SetStep(Step::SignedIn);
            static_cast<void>(FinishAsync(version));
            co_return;
        case winrt::HaloDesktop::SignInOutcome::InvalidCredentials:
            SetLocalError(L"The username or password is incorrect.");
            SetStep(Step::Local);
            co_return;
        case winrt::HaloDesktop::SignInOutcome::RateLimited:
            SetLocalError(L"Too many attempts. Try again later.");
            SetStep(Step::Local);
            co_return;
        default:
            SetLocalError(L"Can't connect to Halo. Try again.");
            SetStep(Step::Local);
            co_return;
        }
    }

    winrt::Windows::Foundation::IAsyncAction LoginViewModel::RunBrowserSignInAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_requestVersion;
        m_waitingForLocal = false;
        RaiseWaitingCopy();
        SetStep(Step::Waiting);

        auto const outcome = co_await m_session->RequestBrowserSignInAsync();
        co_await uiContext;
        if (version != m_requestVersion)
        {
            co_return;
        }

        switch (outcome)
        {
        case winrt::HaloDesktop::SignInOutcome::Succeeded:
            Raise(L"DisplayName");
            SetStep(Step::SignedIn);
            static_cast<void>(FinishAsync(version));
            co_return;
        case winrt::HaloDesktop::SignInOutcome::Declined:
            SetStep(Step::Declined);
            co_return;
        case winrt::HaloDesktop::SignInOutcome::Expired:
            SetStep(Step::Expired);
            co_return;
        default:
            SetStep(Step::Unreachable);
            co_return;
        }
    }

    winrt::Windows::Foundation::IAsyncAction LoginViewModel::FinishAsync(std::uint32_t version)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        co_await winrt::resume_after(SignedInDwell);
        co_await uiContext;

        if (version != m_requestVersion)
        {
            co_return;
        }

        m_navigation->GoTo(::HaloDesktop::Services::Page::Home);
        m_navigation->CloseOverlay();
        SetStep(m_session->Mode() == ::HaloDesktop::Services::AuthenticationMode::Local
            ? Step::Local
            : Step::Oidc);
    }

    void LoginViewModel::SetStep(Step step)
    {
        if (m_step == step)
        {
            return;
        }
        m_step = step;
        if (m_detailsOpen)
        {
            m_detailsOpen = false;
            Raise(L"DetailsVisibility");
            Raise(L"DetailsGlyph");
        }
        RaiseSteps();
    }

    void LoginViewModel::SetLocalError(winrt::hstring value)
    {
        if (m_localError == value)
        {
            return;
        }
        m_localError = std::move(value);
        Raise(L"LocalErrorText");
        Raise(L"LocalErrorVisibility");
    }

    winrt::event_token LoginViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void LoginViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void LoginViewModel::RaiseSteps()
    {
        Raise(L"DiscoveringVisibility");
        Raise(L"LocalVisibility");
        Raise(L"OidcVisibility");
        Raise(L"WaitingVisibility");
        Raise(L"WaitingCancelVisibility");
        Raise(L"SignedInVisibility");
        Raise(L"DeclinedVisibility");
        Raise(L"ExpiredVisibility");
        Raise(L"UnreachableVisibility");
    }

    void LoginViewModel::RaiseWaitingCopy()
    {
        Raise(L"WaitingTitle");
        Raise(L"WaitingBody");
        Raise(L"WaitingStatus");
        Raise(L"WaitingCancelVisibility");
    }

    void LoginViewModel::Raise(wchar_t const* propertyName)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName);
    }
}
