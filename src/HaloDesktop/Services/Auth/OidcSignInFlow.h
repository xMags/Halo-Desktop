#pragma once

#include "Api/Dto.h"
#include "Services/Auth/SessionStore.h"

#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <pplawait.h>
#include <ppltasks.h>
#include <winrt/HaloDesktop.h>

namespace HaloDesktop::Api
{
    class HttpExecutor;
}

namespace HaloDesktop::Services::Auth
{
    struct OidcSignInResult final
    {
        winrt::HaloDesktop::SignInOutcome Outcome{ winrt::HaloDesktop::SignInOutcome::Unreachable };
        std::optional<StoredOidcSession> Session;
    };

    // Thread-safe OIDC authorization-code flow. The listener is bound before
    // the browser opens, and endpoint strings are used exactly as discovered.
    class OidcSignInFlow final
    {
    public:
        explicit OidcSignInFlow(std::shared_ptr<::HaloDesktop::Api::HttpExecutor> executor);

        [[nodiscard]] concurrency::task<OidcSignInResult> SignInAsync(
            ::HaloDesktop::Api::Dto::AuthConfig config);
        void Cancel() noexcept;

    private:
        [[nodiscard]] std::stop_token BeginAttempt();

        std::shared_ptr<::HaloDesktop::Api::HttpExecutor> m_executor;
        std::mutex m_cancellationMutex;
        std::stop_source m_cancellation;
    };
}
