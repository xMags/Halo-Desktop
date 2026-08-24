#include "pch.h"
#include "Services/SessionService.h"
#include "Config/ServerConfig.h"

#include <chrono>
#include <string>
#include <winrt/Windows.Storage.h>

namespace
{
    // Stands in for the identity the server would return once sign-in is real.
    constexpr wchar_t SignedInUserName[] = L"debashis";

    constexpr wchar_t UserNameKey[] = L"Session.UserName";
    constexpr wchar_t SignedInKey[] = L"Session.IsSignedIn";

    // Written by the removed Connect screen. Cleared on load so an upgraded
    // install does not keep a server address the app can no longer act on.
    constexpr wchar_t LegacyServerUrlKey[] = L"Session.ServerUrl";

    winrt::Windows::Foundation::Collections::IPropertySet LocalValues()
    {
        return winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values();
    }

    winrt::Windows::Foundation::IInspectable ReadValue(
        winrt::Windows::Foundation::Collections::IPropertySet const& values,
        wchar_t const* key)
    {
        return values.HasKey(key) ? values.Lookup(key) : nullptr;
    }

    void RemoveIfPresent(
        winrt::Windows::Foundation::Collections::IPropertySet const& values,
        wchar_t const* key)
    {
        if (values.HasKey(key))
        {
            values.Remove(key);
        }
    }
}

namespace HaloDesktop::Services
{
    SessionService::SessionService()
    {
        auto const values = LocalValues();
        RemoveIfPresent(values, LegacyServerUrlKey);

        m_userName = winrt::unbox_value_or<winrt::hstring>(ReadValue(values, UserNameKey), L"");
        m_isSignedIn = winrt::unbox_value_or<bool>(ReadValue(values, SignedInKey), false)
            && !m_userName.empty();
    }

    winrt::hstring SessionService::ServerUrl() const
    {
        std::wstring serverUrl{ ::HaloDesktop::Config::ServerBaseUrl };
        while (!serverUrl.empty() && serverUrl.back() == L'/')
        {
            serverUrl.pop_back();
        }
        return winrt::hstring{ serverUrl };
    }

    winrt::hstring SessionService::UserName() const
    {
        return m_userName;
    }

    bool SessionService::IsSignedIn() const noexcept
    {
        return m_isSignedIn;
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::HaloDesktop::SignInOutcome>
        SessionService::RequestBrowserSignInAsync()
    {
        // Prototype stand-in for the real flow: open the browser, listen on a
        // loopback redirect, and resolve when the server hands back a session.
        // The delay is the round trip; the other outcomes are wired through the
        // UI but cannot be produced until a real server is on the other end.
        auto const uiContext = winrt::apartment_context{};
        co_await winrt::resume_after(std::chrono::milliseconds(3200));
        co_await uiContext;

        m_userName = winrt::hstring{ SignedInUserName };
        m_isSignedIn = true;
        PersistSession();
        co_return winrt::HaloDesktop::SignInOutcome::Succeeded;
    }

    void SessionService::SignOut()
    {
        auto const values = LocalValues();
        RemoveIfPresent(values, SignedInKey);
        RemoveIfPresent(values, UserNameKey);
        m_userName.clear();
        m_isSignedIn = false;
    }

    void SessionService::PersistSession()
    {
        auto const values = LocalValues();
        values.Insert(UserNameKey, winrt::box_value(m_userName));
        values.Insert(SignedInKey, winrt::box_value(m_isSignedIn));
    }
}
