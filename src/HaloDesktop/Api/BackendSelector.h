#pragma once

#include <memory>
#include <mutex>

namespace HaloDesktop::Api
{
    class ApiClient;
    class IHaloBackend;

    // Which backend the services talk to right now: the account's ApiClient,
    // or the on-device backend while Halo is used without an account. The
    // selector only holds the answer; the session layer decides when to
    // switch and runs the identity-changed path that resets every service
    // with each switch.
    //
    // Thread-safe: every method may be called from any thread. A service asks
    // Current() immediately before each backend call and never keeps the
    // answer in a member, so a switch reaches every later call; the services'
    // own account version checks discard answers that straddle a switch.
    class BackendSelector final
    {
    public:
        explicit BackendSelector(std::shared_ptr<ApiClient> account);

        BackendSelector(BackendSelector const&) = delete;
        BackendSelector& operator=(BackendSelector const&) = delete;

        [[nodiscard]] std::shared_ptr<IHaloBackend> Current() const;
        // True while Current() is the on-device backend.
        [[nodiscard]] bool UsesDevice() const;

        // Answers every later Current() from `device`, which must not be null.
        void UseDevice(std::shared_ptr<IHaloBackend> device);
        // Answers every later Current() from the account's client again.
        void UseAccount();

    private:
        mutable std::mutex m_mutex;
        std::shared_ptr<ApiClient> m_account;
        std::shared_ptr<IHaloBackend> m_current;
        bool m_usesDevice{};
    };
}
