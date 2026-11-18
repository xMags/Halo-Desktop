#pragma once

#include <memory>
#include <mutex>

namespace HaloDesktop::Api
{
    class ApiClient;
    class IHaloBackend;

    // Which backend the services talk to right now. Today that is always the
    // account's ApiClient; Halo used without an account will switch it to the
    // on-device backend.
    //
    // Thread-safe: Current() may be called from any thread. A service asks it
    // immediately before each backend call and never keeps the answer in a
    // member, so a switch reaches every later call; the services' own account
    // version checks discard answers that straddle a switch.
    class BackendSelector final
    {
    public:
        explicit BackendSelector(std::shared_ptr<ApiClient> account);

        BackendSelector(BackendSelector const&) = delete;
        BackendSelector& operator=(BackendSelector const&) = delete;

        [[nodiscard]] std::shared_ptr<IHaloBackend> Current() const;

    private:
        mutable std::mutex m_mutex;
        std::shared_ptr<IHaloBackend> m_current;
    };
}
