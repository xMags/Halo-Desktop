#include "Playback/MpvCallQueue.h"

#include <utility>

namespace HaloDesktop::Playback
{
    MpvCallQueue::MpvCallQueue()
        : m_worker([this]() { Run(); })
    {
    }

    MpvCallQueue::~MpvCallQueue()
    {
        Shutdown();
    }

    bool MpvCallQueue::Enqueue(std::function<void()> call)
    {
        if (!call)
        {
            return false;
        }
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_stopping)
            {
                return false;
            }
            m_calls.push_back(std::move(call));
        }
        m_ready.notify_one();
        return true;
    }

    void MpvCallQueue::Shutdown() noexcept
    {
        std::deque<std::function<void()>> discarded;
        {
            std::scoped_lock const lock{ m_mutex };
            m_stopping = true;
            // Destroyed outside the lock: a call's captures may hold anything.
            discarded.swap(m_calls);
        }
        m_ready.notify_all();
        // Joining from the worker itself would deadlock; the queue's owners never
        // shut it down from inside one of its calls.
        if (m_worker.joinable() && m_worker.get_id() != std::this_thread::get_id())
        {
            m_worker.join();
        }
    }

    void MpvCallQueue::Run() noexcept
    {
        for (;;)
        {
            std::function<void()> call;
            {
                std::unique_lock lock{ m_mutex };
                m_ready.wait(lock, [this]() { return m_stopping || !m_calls.empty(); });
                if (m_stopping)
                {
                    return;
                }
                call = std::move(m_calls.front());
                m_calls.pop_front();
            }
            try
            {
                call();
            }
            catch (...)
            {
                // A failed call reports through its own path, if it has one, and
                // never holds up the calls behind it.
            }
        }
    }
}
