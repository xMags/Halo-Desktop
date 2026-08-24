#pragma once

#include "Services/ServiceInterfaces.h"

namespace HaloDesktop::Services
{
    // UI-thread-only. The server is fixed at build time: there is no manual
    // server selection, so only the signed-in identity is persisted. Sign-in is
    // handed to the browser, so no credential ever reaches this process.
    class SessionService final : public ISessionService
    {
    public:
        SessionService();

        [[nodiscard]] winrt::hstring ServerUrl() const override;
        [[nodiscard]] winrt::hstring UserName() const override;
        [[nodiscard]] bool IsSignedIn() const noexcept override;
        winrt::Windows::Foundation::IAsyncOperation<winrt::HaloDesktop::SignInOutcome>
            RequestBrowserSignInAsync() override;
        void SignOut() override;

    private:
        void PersistSession();

        winrt::hstring m_userName;
        bool m_isSignedIn{};
    };
}
