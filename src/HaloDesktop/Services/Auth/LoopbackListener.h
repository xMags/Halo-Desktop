#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <ppltasks.h>
#include <winrt/base.h>

namespace HaloDesktop::Services::Auth
{
    // Whether a callback's path and query (as received, still encoded) carry
    // `state` equal to the expected value once decoded. The first `state`
    // counts, as BrowserCallback::Value reads it.
    [[nodiscard]] bool CallbackCarriesState(std::string_view path, std::wstring_view expectedState);

    // One-shot RFC 8252 loopback listener. Construction binds the fixed port;
    // the accept loop then runs on one owned jthread until callback, cancel, or
    // timeout. Only Cancel() (or destruction) cancels it: no request another
    // program or page can send does.
    //
    // With an expected state, only a `/callback` carrying that state completes
    // it (a provider's `error=...` answer included); any other request is
    // answered 404 and the listener keeps waiting. Without one, the first
    // `/callback` completes it and the caller checks the state itself.
    class LoopbackListener final
    {
    public:
        explicit LoopbackListener(
            std::chrono::seconds timeout = std::chrono::seconds{ 300 },
            std::optional<std::wstring> expectedState = std::nullopt);
        ~LoopbackListener();

        LoopbackListener(LoopbackListener const&) = delete;
        LoopbackListener& operator=(LoopbackListener const&) = delete;

        [[nodiscard]] concurrency::task<winrt::hstring> WaitAsync();
        void Cancel() noexcept;

    private:
        struct State;
        std::unique_ptr<State> m_state;
    };
}
