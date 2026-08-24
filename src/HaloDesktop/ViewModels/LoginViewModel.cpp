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

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    // How long the signed-in card stays up before the shell takes over. Long
    // enough to read the identity, short enough not to feel like a stall.
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
    }

    winrt::hstring LoginViewModel::ServerHost() const { return m_serverHost; }
    winrt::hstring LoginViewModel::DisplayName() const { return CapitalizeFirst(m_session->UserName()); }

    Microsoft::UI::Xaml::Visibility LoginViewModel::IdleVisibility() const noexcept { return m_step == Step::Idle ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::WaitingVisibility() const noexcept { return m_step == Step::Waiting ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::SignedInVisibility() const noexcept { return m_step == Step::SignedIn ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::DeclinedVisibility() const noexcept { return m_step == Step::Declined ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::ExpiredVisibility() const noexcept { return m_step == Step::Expired ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::UnreachableVisibility() const noexcept { return m_step == Step::Unreachable ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility LoginViewModel::DetailsVisibility() const noexcept { return m_detailsOpen ? Visible : Collapsed; }
    // Segoe Fluent Icons: ChevronDown when open, ChevronRight when closed.
    winrt::hstring LoginViewModel::DetailsGlyph() const { return winrt::hstring{ m_detailsOpen ? L"\uE70D" : L"\uE76C" }; }

    void LoginViewModel::StartSignIn()
    {
        if (m_step == Step::Waiting)
        {
            return;
        }
        static_cast<void>(RunSignInAsync());
    }

    void LoginViewModel::Reopen()
    {
        // A real implementation re-launches the browser against the request that
        // is already open. Restarting the round trip keeps the states honest.
        static_cast<void>(RunSignInAsync());
    }

    void LoginViewModel::Cancel()
    {
        ++m_requestVersion;
        SetStep(Step::Idle);
    }

    void LoginViewModel::ToggleDetails()
    {
        m_detailsOpen = !m_detailsOpen;
        Raise(L"DetailsVisibility");
        Raise(L"DetailsGlyph");
    }

    winrt::Windows::Foundation::IAsyncAction LoginViewModel::RunSignInAsync()
    {
        auto lifetime = get_strong();
        auto const version = ++m_requestVersion;
        SetStep(Step::Waiting);

        auto const outcome = co_await m_session->RequestBrowserSignInAsync();
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
        case winrt::HaloDesktop::SignInOutcome::Unreachable:
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
        SetStep(Step::Idle);
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
        Raise(L"IdleVisibility");
        Raise(L"WaitingVisibility");
        Raise(L"SignedInVisibility");
        Raise(L"DeclinedVisibility");
        Raise(L"ExpiredVisibility");
        Raise(L"UnreachableVisibility");
    }

    void LoginViewModel::Raise(wchar_t const* propertyName)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName);
    }
}
