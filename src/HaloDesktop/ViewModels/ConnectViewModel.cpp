#include "pch.h"
#include "ViewModels/ConnectViewModel.h"
#if __has_include("ConnectViewModel.g.cpp")
#include "ConnectViewModel.g.cpp"
#endif

#include "Services/NavigationService.h"
#include "ViewModels/ObservableHelper.h"

namespace winrt::HaloDesktop::implementation
{
    ConnectViewModel::ConnectViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_session(services.Session),
          m_navigation(services.Navigation),
          m_serverUrl(services.Session->ServerUrl().empty()
              ? winrt::hstring{ L"https://halo.ditto.moe" }
              : services.Session->ServerUrl())
    {
    }

    winrt::hstring ConnectViewModel::ServerUrl() const { return m_serverUrl; }

    void ConnectViewModel::ServerUrl(winrt::hstring const& value)
    {
        if (m_serverUrl == value)
        {
            return;
        }

        m_serverUrl = value;
        ++m_requestVersion;
        m_isReached = false;
        m_statusVisible = false;
        Raise(L"ServerUrl");
        RaiseState();
    }

    bool ConnectViewModel::IsTesting() const noexcept { return m_isTesting; }
    bool ConnectViewModel::IsReached() const noexcept { return m_isReached; }
    bool ConnectViewModel::CanTest() const noexcept { return !m_isTesting && !m_serverUrl.empty(); }
    bool ConnectViewModel::CanContinue() const noexcept { return m_isReached && !m_isTesting; }
    winrt::hstring ConnectViewModel::StatusText() const { return m_statusText; }
    Microsoft::UI::Xaml::Visibility ConnectViewModel::StatusVisibility() const noexcept
    {
        return m_statusVisible
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed;
    }

    Windows::Foundation::IAsyncAction ConnectViewModel::TestServerAsync()
    {
        auto lifetime = get_strong();
        auto const version = ++m_requestVersion;
        auto const requestedUrl = m_serverUrl;
        m_isTesting = true;
        m_isReached = false;
        m_statusVisible = false;
        RaiseState();

        auto const reached = co_await m_session->TestServerAsync(requestedUrl);
        if (version != m_requestVersion)
        {
            co_return;
        }

        m_isTesting = false;
        m_statusVisible = true;
        m_isReached = reached;
        m_statusText = reached ? L"REACHED · LOCAL AUTH · v0.9.4" : L"HTTPS REQUIRED";
        if (reached)
        {
            m_session->SetServerUrl(requestedUrl);
        }
        RaiseState();
    }

    void ConnectViewModel::Continue()
    {
        if (!CanContinue())
        {
            return;
        }
        m_navigation->ShowOverlay(::HaloDesktop::Services::Page::Login);
    }

    winrt::event_token ConnectViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void ConnectViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void ConnectViewModel::Raise(wchar_t const* propertyName)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName);
    }

    void ConnectViewModel::RaiseState()
    {
        Raise(L"IsTesting");
        Raise(L"IsReached");
        Raise(L"CanTest");
        Raise(L"CanContinue");
        Raise(L"StatusText");
        Raise(L"StatusVisibility");
    }
}
