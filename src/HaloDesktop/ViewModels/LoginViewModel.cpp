#include "pch.h"
#include "ViewModels/LoginViewModel.h"
#if __has_include("LoginViewModel.g.cpp")
#include "LoginViewModel.g.cpp"
#endif

#include "Services/Auth/ServerAddressPolicy.h"
#include "Services/Auth/SignInFailurePolicy.h"
#include "Services/Device/DeviceDataMove.h"
#include "Services/Device/DeviceModePolicy.h"
#include "Services/NavigationService.h"
#include "ViewModels/ObservableHelper.h"

#include <chrono>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <winrt/Windows.System.h>

namespace
{
    namespace Policy = ::HaloDesktop::ViewModels;
    namespace Auth = ::HaloDesktop::Services::Auth;

    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    // How long "You're all set" stays up before the library opens.
    constexpr std::chrono::milliseconds SignedInDwell{ 1200 };

    winrt::Microsoft::UI::Xaml::Visibility VisibleWhen(bool condition) noexcept
    {
        return condition ? Visible : Collapsed;
    }

    // Every property a step change can move; raised together.
    constexpr wchar_t const* StepProperties[] = {
        L"IntroVisibility", L"DiscoveringVisibility", L"BrowserVisibility", L"FormVisibility",
        L"ServerFieldVisibility", L"WaitingVisibility", L"WaitingBrowserVisibility",
        L"SignedInVisibility", L"ResultVisibility", L"UnsupportedVisibility", L"UnreachableVisibility",
        L"PreparingVisibility", L"NoAccountVisibility", L"PasswordLinkVisibility", L"DetailsVisibility",
        L"ErrorVisibility", L"AccountNameVisibility", L"DiscoveringActive", L"WaitingActive",
        L"PreparingActive", L"SignedInActive", L"ReopenEnabled", L"ErrorText", L"MethodLabel",
        L"FormLinkText", L"WaitingTitle", L"WaitingBody", L"WaitingStatus", L"ResultTitle",
        L"ResultBody", L"UnsupportedBody", L"Detail", L"DetailsGlyph", L"AccountName",
        L"AccountLabel", L"AvatarInitial", L"SignedInStatus", L"ServerFieldFirst", L"NoAccountLabel",
        L"DeviceErrorText", L"DeviceErrorVisibility",
    };

    std::wstring DescribeFailure(std::exception_ptr const& failure)
    {
        try
        {
            if (failure)
            {
                std::rethrow_exception(failure);
            }
        }
        catch (winrt::hresult_error const& error)
        {
            return Auth::SanitizedSignInText(std::wstring_view{ error.message() });
        }
        catch (std::exception const& error)
        {
            return Auth::SanitizedSignInText(std::wstring{ winrt::to_hstring(error.what()) });
        }
        catch (...)
        {
        }
        return L"Unknown error";
    }
}

namespace winrt::HaloDesktop::implementation
{
    LoginViewModel::LoginViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_session(services.Session),
          m_navigation(services.Navigation),
          m_deviceMove(services.DeviceMove),
          m_deviceError(services.Session->StartupDeviceError())
    {
        DiscoverAsync();
        LoadLastServerAsync();
    }

    winrt::hstring LoginViewModel::Username() const { return m_username; }
    void LoginViewModel::Username(winrt::hstring const& value)
    {
        if (m_username == value) return;
        m_username = value;
        Raise(L"Username");
    }

    winrt::hstring LoginViewModel::ServerAddress() const { return m_serverAddress; }
    void LoginViewModel::ServerAddress(winrt::hstring const& value)
    {
        if (m_serverAddress == value) return;
        m_serverAddress = value;
        m_serverAddressEdited = true;
        Raise(L"ServerAddress");
    }

    bool LoginViewModel::Is(StepKind kind) const noexcept { return m_step.Kind == kind; }

    Microsoft::UI::Xaml::Visibility LoginViewModel::IntroVisibility() const noexcept { return VisibleWhen(Policy::IntroVisible(m_step.Kind)); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::DiscoveringVisibility() const noexcept { return VisibleWhen(Is(StepKind::Discovering)); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::BrowserVisibility() const noexcept { return VisibleWhen(Is(StepKind::Browser)); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::FormVisibility() const noexcept { return VisibleWhen(Is(StepKind::Local) || Is(StepKind::Custom)); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::ServerFieldVisibility() const noexcept { return VisibleWhen(Is(StepKind::Custom)); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::WaitingVisibility() const noexcept { return VisibleWhen(Is(StepKind::Waiting)); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::WaitingBrowserVisibility() const noexcept { return VisibleWhen(Is(StepKind::Waiting) && !m_step.WaitingLocal); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::SignedInVisibility() const noexcept { return VisibleWhen(Is(StepKind::SignedIn)); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::ResultVisibility() const noexcept { return VisibleWhen(Is(StepKind::Declined) || Is(StepKind::Expired)); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::UnsupportedVisibility() const noexcept { return VisibleWhen(Is(StepKind::Unsupported)); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::UnreachableVisibility() const noexcept { return VisibleWhen(Is(StepKind::Unreachable)); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::PreparingVisibility() const noexcept { return VisibleWhen(Is(StepKind::Preparing)); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::NoAccountVisibility() const noexcept
    {
        return VisibleWhen(Policy::NoAccountModeAvailable && Policy::NoAccountStep(m_step.Kind));
    }
    Microsoft::UI::Xaml::Visibility LoginViewModel::PasswordLinkVisibility() const noexcept { return VisibleWhen(Policy::PasswordLinkVisible(m_step.Kind)); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::DetailsVisibility() const noexcept { return VisibleWhen(m_detailsOpen && Is(StepKind::Unreachable)); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::ErrorVisibility() const noexcept { return VisibleWhen(!m_step.Error.empty()); }
    Microsoft::UI::Xaml::Visibility LoginViewModel::AccountNameVisibility() const noexcept { return VisibleWhen(!m_step.AccountName.empty()); }
    bool LoginViewModel::DiscoveringActive() const noexcept { return Is(StepKind::Discovering); }
    bool LoginViewModel::WaitingActive() const noexcept { return Is(StepKind::Waiting); }
    bool LoginViewModel::PreparingActive() const noexcept { return Is(StepKind::Preparing); }
    bool LoginViewModel::SignedInActive() const noexcept { return Is(StepKind::SignedIn); }
    bool LoginViewModel::ReopenEnabled() const noexcept { return Is(StepKind::Waiting) && m_step.AuthUrlKnown && !m_authUrl.empty(); }

    winrt::hstring LoginViewModel::ErrorText() const { return winrt::hstring{ m_step.Error }; }
    winrt::hstring LoginViewModel::MethodLabel() const
    {
        return m_step.Method ? winrt::hstring{ Policy::MethodLabel(*m_step.Method) } : winrt::hstring{};
    }
    winrt::hstring LoginViewModel::FormLinkText() const
    {
        if (Is(StepKind::Custom) && m_step.Back)
        {
            return winrt::hstring{ Policy::BackLabel(Policy::Settled(*m_step.Back, m_discovered)) };
        }
        return L"Use another server";
    }
    winrt::hstring LoginViewModel::WaitingTitle() const { return winrt::hstring{ Policy::WaitingTitle(m_step.WaitingLocal) }; }
    winrt::hstring LoginViewModel::WaitingBody() const { return winrt::hstring{ Policy::WaitingBody(m_step.WaitingLocal) }; }
    winrt::hstring LoginViewModel::WaitingStatus() const { return winrt::hstring{ Policy::WaitingStatus(m_step.WaitingLocal) }; }
    winrt::hstring LoginViewModel::ResultTitle() const
    {
        return Is(StepKind::Declined) ? L"Sign-in was declined" : L"Sign-in request expired";
    }
    winrt::hstring LoginViewModel::ResultBody() const
    {
        return Is(StepKind::Declined)
            ? L"You can try again whenever you’re ready."
            : L"This sign-in request has expired. Start a new one to continue.";
    }
    winrt::hstring LoginViewModel::UnsupportedBody() const { return winrt::hstring{ Policy::UnsupportedBody(m_step.Mode) }; }
    winrt::hstring LoginViewModel::Detail() const { return winrt::hstring{ m_step.Detail }; }
    winrt::hstring LoginViewModel::DetailsGlyph() const { return m_detailsOpen ? L"" : L""; }
    winrt::hstring LoginViewModel::AccountName() const { return winrt::hstring{ m_step.AccountName }; }
    winrt::hstring LoginViewModel::AccountLabel() const { return winrt::hstring{ m_step.AccountLabel }; }
    winrt::hstring LoginViewModel::AvatarInitial() const { return winrt::hstring{ Policy::AvatarInitial(m_step.AccountName) }; }
    winrt::hstring LoginViewModel::SignedInStatus() const { return winrt::hstring{ Policy::SignedInStatus(m_step.Moving) }; }
    winrt::hstring LoginViewModel::NoAccountLabel() const { return winrt::hstring{ Policy::NoAccountLabel(OpenedFromDevice()) }; }
    winrt::hstring LoginViewModel::DeviceErrorText() const { return winrt::hstring{ m_deviceError }; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::DeviceErrorVisibility() const noexcept
    {
        return VisibleWhen(!m_deviceError.empty() && Policy::NoAccountStep(m_step.Kind));
    }
    bool LoginViewModel::OpenedFromDevice() const noexcept
    {
        return m_session->Mode() == ::HaloDesktop::Services::SessionMode::Device;
    }
    bool LoginViewModel::ServerFieldFirst() const noexcept { return Is(StepKind::Custom) && m_serverAddress.empty(); }

    void LoginViewModel::Retry()
    {
        if (Is(StepKind::Unreachable) || Is(StepKind::Unsupported))
        {
            DiscoverAsync();
        }
    }

    void LoginViewModel::StartBrowserSignIn()
    {
        auto const resting = Is(StepKind::Browser) || Is(StepKind::Declined) || Is(StepKind::Expired);
        if (!resting || !m_step.Method)
        {
            return;
        }
        StartBrowserAsync(*m_step.Method);
    }

    void LoginViewModel::OpenLinkAgain()
    {
        if (!ReopenEnabled())
        {
            return;
        }
        try
        {
            auto const opened = winrt::Windows::System::Launcher::LaunchUriAsync(
                winrt::Windows::Foundation::Uri{ m_authUrl });
            opened.Completed([](auto const& operation, auto)
            {
                try { static_cast<void>(operation.GetResults()); } catch (...) {}
            });
        }
        catch (winrt::hresult_error const&)
        {
            // The browser wait goes on; the person can cancel and start again.
        }
    }

    void LoginViewModel::CancelBrowserSignIn()
    {
        if (!Is(StepKind::Waiting) || m_step.WaitingLocal || !m_step.Method)
        {
            return;
        }
        auto const method = *m_step.Method;
        ++m_attempt;
        m_session->CancelSignIn();
        m_authUrl.clear();
        Step browser;
        browser.Kind = StepKind::Browser;
        browser.Method = method;
        SetStep(std::move(browser));
    }

    void LoginViewModel::Submit(winrt::hstring const& password)
    {
        if (Is(StepKind::Local))
        {
            if (m_username.empty() || password.empty())
            {
                Step local;
                local.Kind = StepKind::Local;
                local.Error = std::wstring{ Policy::MissingCredentialsError };
                SetStep(std::move(local));
                return;
            }
            SubmitLocalAsync(password);
            return;
        }
        if (Is(StepKind::Custom) && m_step.Back)
        {
            SubmitCustomAsync(password, *m_step.Back);
        }
    }

    void LoginViewModel::FormLink()
    {
        if (Is(StepKind::Local))
        {
            OpenCustom();
            return;
        }
        if (Is(StepKind::Custom) && m_step.Back)
        {
            SetStep(Policy::ToStep(Policy::Settled(*m_step.Back, m_discovered)));
        }
    }

    void LoginViewModel::OpenCustom()
    {
        if (auto custom = Policy::OpenCustom(m_step))
        {
            SetStep(std::move(*custom));
        }
    }

    void LoginViewModel::ToggleDetails()
    {
        if (!Is(StepKind::Unreachable))
        {
            return;
        }
        m_detailsOpen = !m_detailsOpen;
        Raise(L"DetailsVisibility");
        Raise(L"DetailsGlyph");
    }

    void LoginViewModel::ContinueWithoutAccount()
    {
        if (!Policy::NoAccountStep(m_step.Kind))
        {
            return;
        }
        if (OpenedFromDevice())
        {
            // "Back to Halo": the sign-in screen goes and Halo without an
            // account is as it was. Closing it cancels any sign-in under way.
            m_navigation->CloseOverlay();
            return;
        }
        ContinueOnDeviceAsync();
    }

    winrt::fire_and_forget LoginViewModel::ContinueOnDeviceAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const id = ++m_attempt;
        auto const previous = m_step;
        m_deviceError.clear();
        Step preparing;
        preparing.Kind = StepKind::Preparing;
        SetStep(std::move(preparing));

        std::exception_ptr failure;
        try
        {
            co_await m_session->ContinueWithoutAccountAsync();
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        co_await uiContext;
        if (id != m_attempt)
        {
            co_return;
        }
        if (!failure)
        {
            m_navigation->GoTo(::HaloDesktop::Services::Page::Home);
            m_navigation->CloseOverlay();
            co_return;
        }
        m_deviceError = ::HaloDesktop::Services::Device::DeviceSetupError(DescribeFailure(failure));
        // Back where it was; a server answer that came meanwhile is shown now.
        if (previous.Kind == StepKind::Discovering && m_discovered)
        {
            SetStep(Policy::ToStep(*m_discovered));
        }
        else
        {
            SetStep(previous);
        }
        Raise(L"DeviceErrorText");
        Raise(L"DeviceErrorVisibility");
    }

    void LoginViewModel::Close()
    {
        if (m_closed)
        {
            return;
        }
        m_closed = true;
        ++m_attempt;
        ++m_discovery;
        m_session->CancelSignIn();
    }

    winrt::fire_and_forget LoginViewModel::DiscoverAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const id = ++m_discovery;
        m_discovered.reset();
        SetStep(Step{});

        std::optional<::HaloDesktop::ViewModels::RestingStep> found;
        std::exception_ptr failure;
        try
        {
            auto const discovery = co_await m_session->DiscoverAuthenticationAsync();
            found = Policy::ReadyStep(
                discovery.ModeName,
                discovery.Mode == ::HaloDesktop::Services::SignInMode::Addon
                    ? std::optional<std::wstring>{ discovery.ButtonLabel }
                    : std::nullopt);
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        co_await uiContext;
        if (id != m_discovery)
        {
            co_return;
        }
        if (!found)
        {
            found = Policy::UnreachableStep(DescribeFailure(failure));
        }
        m_discovered = found;
        m_detailsOpen = false;
        SetStep(Policy::ApplyDiscovery(m_step, *found));
        // The custom form's back link names where it now goes.
        Raise(L"FormLinkText");
    }

    winrt::fire_and_forget LoginViewModel::LoadLastServerAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        winrt::hstring remembered;
        try
        {
            remembered = co_await m_session->LastServerAddressAsync();
        }
        catch (...)
        {
            co_return;
        }
        co_await uiContext;
        // What the person has typed meanwhile wins.
        if (m_serverAddressEdited || remembered.empty())
        {
            co_return;
        }
        m_serverAddress = remembered;
        Raise(L"ServerAddress");
        Raise(L"ServerFieldFirst");
    }

    winrt::fire_and_forget LoginViewModel::SubmitLocalAsync(winrt::hstring password)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const id = ++m_attempt;
        Step waiting;
        waiting.Kind = StepKind::Waiting;
        waiting.WaitingLocal = true;
        SetStep(std::move(waiting));

        ::HaloDesktop::Services::SignInAttempt attempt;
        try
        {
            attempt = co_await m_session->SignInWithPasswordAsync(std::nullopt, m_username, std::move(password));
        }
        catch (...)
        {
            attempt = ::HaloDesktop::Services::SignInAttempt{};
        }
        co_await uiContext;
        if (id != m_attempt || attempt.Outcome == ::HaloDesktop::Services::SignInAttemptOutcome::Superseded)
        {
            co_return;
        }
        if (attempt.Outcome == ::HaloDesktop::Services::SignInAttemptOutcome::Succeeded)
        {
            m_session->AcknowledgeSignIn();
            FinishAsync(id);
            co_return;
        }
        Step local;
        local.Kind = StepKind::Local;
        local.Error = Auth::LocalSignInMessage(attempt.HttpStatus);
        SetStep(std::move(local));
    }

    winrt::fire_and_forget LoginViewModel::SubmitCustomAsync(
        winrt::hstring password,
        ::HaloDesktop::ViewModels::RestingStep back)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const fail = [this, &back](std::wstring error)
        {
            Step custom;
            custom.Kind = StepKind::Custom;
            custom.Error = std::move(error);
            custom.Back = Policy::Settled(back, m_discovered);
            SetStep(std::move(custom));
        };

        auto const parsed = Auth::ParseServerAddress(std::wstring_view{ m_serverAddress });
        if (!parsed.Address)
        {
            fail(parsed.Error);
            co_return;
        }
        if (m_username.empty() || password.empty())
        {
            fail(std::wstring{ Policy::MissingCredentialsError });
            co_return;
        }
        auto const address = winrt::hstring{ *parsed.Address };
        auto const username = m_username;
        auto const id = ++m_attempt;
        Step waiting;
        waiting.Kind = StepKind::Waiting;
        waiting.WaitingLocal = true;
        SetStep(std::move(waiting));

        // The server's mode first, so one that signs in through a web page
        // says so instead of refusing the password as if it were wrong.
        std::optional<std::wstring> mode;
        try
        {
            mode = co_await m_session->CheckServerModeAsync(address);
        }
        catch (...)
        {
            mode.reset();
        }
        co_await uiContext;
        if (id != m_attempt)
        {
            co_return;
        }
        if (!mode)
        {
            fail(std::wstring{ Policy::CustomServerUnreachableError });
            co_return;
        }
        if (!Policy::CustomServerAcceptsPasswords(*mode))
        {
            fail(std::wstring{ Policy::CustomServerBrowserModeError });
            co_return;
        }

        ::HaloDesktop::Services::SignInAttempt attempt;
        try
        {
            attempt = co_await m_session->SignInWithPasswordAsync(address, username, std::move(password));
        }
        catch (...)
        {
            attempt = ::HaloDesktop::Services::SignInAttempt{};
        }
        co_await uiContext;
        if (id != m_attempt || attempt.Outcome == ::HaloDesktop::Services::SignInAttemptOutcome::Superseded)
        {
            co_return;
        }
        if (attempt.Outcome == ::HaloDesktop::Services::SignInAttemptOutcome::Succeeded)
        {
            m_session->AcknowledgeSignIn();
            FinishAsync(id);
            co_return;
        }
        fail(Auth::LocalSignInMessage(attempt.HttpStatus));
    }

    winrt::fire_and_forget LoginViewModel::StartBrowserAsync(::HaloDesktop::ViewModels::BrowserMethod method)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const id = ++m_attempt;
        m_authUrl.clear();
        Step waiting;
        waiting.Kind = StepKind::Waiting;
        waiting.WaitingLocal = false;
        waiting.Method = method;
        SetStep(std::move(waiting));

        ::HaloDesktop::Services::SignInAttempt attempt;
        try
        {
            attempt = co_await m_session->RequestBrowserSignInAsync(
                [weak = get_weak(), id](winrt::hstring const& authUrl)
                {
                    if (auto const self = weak.get())
                    {
                        self->OnAuthUrl(id, authUrl);
                    }
                });
        }
        catch (...)
        {
            attempt = ::HaloDesktop::Services::SignInAttempt{
                .Outcome = ::HaloDesktop::Services::SignInAttemptOutcome::Failed,
                .Error = Auth::SignInErrorInfo{ .Message = DescribeFailure(std::current_exception()) },
                .HttpStatus = std::nullopt,
            };
        }
        co_await uiContext;
        if (id != m_attempt || attempt.Outcome == ::HaloDesktop::Services::SignInAttemptOutcome::Superseded)
        {
            co_return;
        }
        if (attempt.Outcome == ::HaloDesktop::Services::SignInAttemptOutcome::Succeeded)
        {
            m_session->AcknowledgeSignIn();
            FinishAsync(id);
            co_return;
        }
        m_authUrl.clear();
        m_detailsOpen = false;
        SetStep(Policy::StepAfterBrowserFailure(method, Auth::ClassifySignInFailure(attempt.Error)));
    }

    void LoginViewModel::OnAuthUrl(std::uint64_t attempt, winrt::hstring const& authUrl)
    {
        if (attempt != m_attempt || !Is(StepKind::Waiting) || m_step.WaitingLocal || authUrl.empty())
        {
            return;
        }
        m_authUrl = authUrl;
        auto next = m_step;
        next.AuthUrlKnown = true;
        SetStep(std::move(next));
    }

    winrt::fire_and_forget LoginViewModel::FinishAsync(std::uint64_t attempt)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        Step signedIn;
        signedIn.Kind = StepKind::SignedIn;
        signedIn.AccountName = Policy::AccountDisplayName(std::wstring_view{ m_session->UserName() });
        auto const plan = m_session->Plan();
        signedIn.AccountLabel = Policy::AccountLabel(
            m_session->IsAdmin(),
            plan ? std::optional<std::wstring_view>{ std::wstring_view{ *plan } } : std::nullopt);
        // Signed in from Halo without an account: this PC's data moves into
        // the account while "You're all set" is up, and the library opens
        // once both the move and the dwell are over.
        signedIn.Moving = m_session->HasDeviceDataToMove();
        auto const moving = signedIn.Moving;
        SetStep(std::move(signedIn));

        std::optional<concurrency::task<bool>> move;
        if (moving && m_deviceMove)
        {
            move = m_deviceMove->RunAsync(true);
        }
        co_await winrt::resume_after(SignedInDwell);
        co_await uiContext;
        if (move)
        {
            // A move that did not finish is tried again at the next launch.
            static_cast<void>(co_await *move);
            co_await uiContext;
        }
        if (attempt != m_attempt)
        {
            co_return;
        }
        m_navigation->GoTo(::HaloDesktop::Services::Page::Home);
        m_navigation->CloseOverlay();
    }

    void LoginViewModel::SetStep(Step step)
    {
        if (m_step == step)
        {
            return;
        }
        auto const kindChanged = m_step.Kind != step.Kind;
        m_step = std::move(step);
        if (kindChanged)
        {
            m_detailsOpen = false;
        }
        for (auto const name : StepProperties)
        {
            Raise(name);
        }
    }

    winrt::event_token LoginViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void LoginViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void LoginViewModel::Raise(wchar_t const* propertyName)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName);
    }
}
