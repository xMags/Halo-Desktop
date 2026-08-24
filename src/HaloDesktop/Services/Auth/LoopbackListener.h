#pragma once

#include <chrono>
#include <memory>
#include <ppltasks.h>
#include <winrt/base.h>

namespace HaloDesktop::Services::Auth
{
    // One-shot RFC 8252 loopback listener. Construction binds the fixed port;
    // the accept loop then runs on one owned jthread until callback, cancel, or
    // timeout.
    class LoopbackListener final
    {
    public:
        explicit LoopbackListener(std::chrono::seconds timeout = std::chrono::seconds{ 300 });
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
