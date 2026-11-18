#pragma once

#include "Api/Dto.h"
#include "Services/Auth/SessionStore.h"
#include "Services/Auth/SignInAddon.h"
#include "Services/Auth/SignInFailurePolicy.h"

#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <pplawait.h>
#include <ppltasks.h>

namespace HaloDesktop::Api
{
    class HttpExecutor;
}

namespace HaloDesktop::Services::Auth
{
    // Either the session the sign-in produced, or why there is none.
    struct OidcSignInResult final
    {
        std::optional<StoredOidcSession> Session;
        SignInErrorInfo Error;
    };

    // Thread-safe OIDC authorization-code flow with PKCE and a state check.
    // The listener is bound before the browser opens, and endpoint strings are
    // used exactly as discovered.
    class OidcSignInFlow final
    {
    public:
        explicit OidcSignInFlow(std::shared_ptr<::HaloDesktop::Api::HttpExecutor> executor);

        // onOpened hears the authorization URL once the browser has it.
        [[nodiscard]] concurrency::task<OidcSignInResult> SignInAsync(
            ::HaloDesktop::Api::Dto::AuthConfig config,
            SignInOpenedHandler onOpened);
        void Cancel() noexcept;

    private:
        [[nodiscard]] std::stop_token BeginAttempt();

        std::shared_ptr<::HaloDesktop::Api::HttpExecutor> m_executor;
        std::mutex m_cancellationMutex;
        std::stop_source m_cancellation;
    };
}
