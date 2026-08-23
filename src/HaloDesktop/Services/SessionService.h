#pragma once

#include "Services/ServiceInterfaces.h"

namespace HaloDesktop::Services
{
    // UI-thread-only. Async validation returns to the calling UI apartment before
    // state changes. Passwords are never retained or persisted.
    class SessionService final : public ISessionService
    {
    public:
        SessionService();

        [[nodiscard]] winrt::hstring ServerUrl() const override;
        [[nodiscard]] winrt::hstring UserName() const override;
        [[nodiscard]] bool IsSignedIn() const noexcept override;
        winrt::Windows::Foundation::IAsyncOperation<bool> TestServerAsync(winrt::hstring url) override;
        void SetServerUrl(winrt::hstring const& url) override;
        bool SignIn(winrt::hstring const& user, winrt::hstring const& password) override;
        void SignOut() override;
        void ClearServer() override;

    private:
        void PersistSession();

        winrt::hstring m_serverUrl;
        winrt::hstring m_userName;
        bool m_isSignedIn{};
    };
}
