#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace HaloDesktop::Playback
{
    // One worker thread that runs calls one at a time, in the order they were
    // queued (the reference client's `createCallQueue`, which the player sends
    // every mpv command through). A call that throws does not hold up the ones
    // behind it. The mpv client uses it so a slow command never blocks the
    // window, and two calls made back to back can never run in either order.
    //
    // Thread safety: Enqueue and Shutdown may be called from any thread except
    // the worker itself. No lock is held while a call runs.
    //
    // Shutdown order, enforced by Shutdown: stop accepting calls, discard the
    // ones not yet started, wait for the one running (if any) to return, join the
    // worker. After that nothing queued here can run, so the owner may destroy
    // whatever the calls used.
    class MpvCallQueue final
    {
    public:
        MpvCallQueue();
        ~MpvCallQueue();

        MpvCallQueue(MpvCallQueue const&) = delete;
        MpvCallQueue& operator=(MpvCallQueue const&) = delete;
        MpvCallQueue(MpvCallQueue&&) = delete;
        MpvCallQueue& operator=(MpvCallQueue&&) = delete;

        // False once Shutdown has begun: the call is dropped, never run.
        [[nodiscard]] bool Enqueue(std::function<void()> call);
        void Shutdown() noexcept;

    private:
        void Run() noexcept;

        std::mutex m_mutex;
        std::condition_variable m_ready;
        std::deque<std::function<void()>> m_calls;
        bool m_stopping{};
        // Declared last so it starts after, and is joined before, the state above.
        std::jthread m_worker;
    };
}
