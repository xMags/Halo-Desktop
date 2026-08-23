#include "pch.h"
#include "Services/SessionService.h"

#include <chrono>
#include <stdexcept>
#include <string_view>
#include <winrt/Windows.Storage.h>

namespace
{
    constexpr wchar_t ServerUrlKey[] = L"Session.ServerUrl";
    constexpr wchar_t UserNameKey[] = L"Session.UserName";
    constexpr wchar_t SignedInKey[] = L"Session.IsSignedIn";

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
        m_serverUrl = winrt::unbox_value_or<winrt::hstring>(ReadValue(values, ServerUrlKey), L"");
        m_userName = winrt::unbox_value_or<winrt::hstring>(ReadValue(values, UserNameKey), L"");
        m_isSignedIn = winrt::unbox_value_or<bool>(ReadValue(values, SignedInKey), false)
            && !m_serverUrl.empty()
            && !m_userName.empty();
    }

    winrt::hstring SessionService::ServerUrl() const
    {
        return m_serverUrl;
    }

    winrt::hstring SessionService::UserName() const
    {
        return m_userName;
    }

    bool SessionService::IsSignedIn() const noexcept
    {
        return m_isSignedIn;
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> SessionService::TestServerAsync(winrt::hstring url)
    {
        auto const uiContext = winrt::apartment_context{};
        co_await winrt::resume_after(std::chrono::milliseconds(400));
        co_await uiContext;
        co_return std::wstring_view(url).starts_with(L"https://");
    }

    void SessionService::SetServerUrl(winrt::hstring const& url)
    {
        if (!std::wstring_view(url).starts_with(L"https://"))
        {
            throw std::invalid_argument("Server URL must use HTTPS");
        }

        m_serverUrl = url;
        auto const values = LocalValues();
        values.Insert(ServerUrlKey, winrt::box_value(m_serverUrl));
    }

    bool SessionService::SignIn(winrt::hstring const& user, [[maybe_unused]] winrt::hstring const& password)
    {
        if (user.empty() || m_serverUrl.empty())
        {
            return false;
        }

        m_userName = user;
        m_isSignedIn = true;
        PersistSession();
        return true;
    }

    void SessionService::SignOut()
    {
        auto const values = LocalValues();
        RemoveIfPresent(values, SignedInKey);
        RemoveIfPresent(values, UserNameKey);
        m_userName.clear();
        m_isSignedIn = false;
    }

    void SessionService::ClearServer()
    {
        auto const values = LocalValues();
        RemoveIfPresent(values, SignedInKey);
        RemoveIfPresent(values, ServerUrlKey);
        RemoveIfPresent(values, UserNameKey);
        m_serverUrl.clear();
        m_userName.clear();
        m_isSignedIn = false;
    }

    void SessionService::PersistSession()
    {
        auto const values = LocalValues();
        values.Insert(ServerUrlKey, winrt::box_value(m_serverUrl));
        values.Insert(UserNameKey, winrt::box_value(m_userName));
        values.Insert(SignedInKey, winrt::box_value(m_isSignedIn));
    }
}
