#include "CallQueueTests.h"

#include "Playback/MpvCallQueue.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    using HaloDesktop::Playback::MpvCallQueue;

    // Records in the order calls actually ran, from the worker thread.
    class Log final
    {
    public:
        void Add(std::string entry)
        {
            std::scoped_lock const lock{ m_mutex };
            m_entries.push_back(std::move(entry));
            m_changed.notify_all();
        }

        [[nodiscard]] bool WaitFor(std::size_t count, std::chrono::milliseconds timeout)
        {
            std::unique_lock lock{ m_mutex };
            return m_changed.wait_for(lock, timeout, [this, count]() { return m_entries.size() >= count; });
        }

        [[nodiscard]] std::vector<std::string> Entries()
        {
            std::scoped_lock const lock{ m_mutex };
            return m_entries;
        }

    private:
        std::mutex m_mutex;
        std::condition_variable m_changed;
        std::vector<std::string> m_entries;
    };

    void TestCallsRunOneAtATimeInOrder()
    {
        // "calls run one at a time in the order they were made": the first is the
        // slowest, so without the queue the others would finish first.
        Log log;
        {
            MpvCallQueue queue;
            auto const call = [&queue, &log](std::string name, std::chrono::milliseconds duration)
            {
                Require(queue.Enqueue([&log, name, duration]()
                {
                    log.Add(name + " start");
                    std::this_thread::sleep_for(duration);
                    log.Add(name + " end");
                }), "a call was refused by a running queue");
            };
            call("a", std::chrono::milliseconds(30));
            call("b", std::chrono::milliseconds(5));
            call("c", std::chrono::milliseconds(1));
            Require(log.WaitFor(6, std::chrono::seconds(5)), "the queued calls did not all run");
        }
        Require(log.Entries() == std::vector<std::string>{ "a start", "a end", "b start", "b end", "c start", "c end" },
            "calls overlapped or ran out of order");
    }

    void TestFailedCallDoesNotHoldUpTheNext()
    {
        // "a failed call reports its error and does not hold up the next"
        Log log;
        MpvCallQueue queue;
        Require(queue.Enqueue([&log]()
        {
            log.Add("failed");
            throw std::runtime_error{ "no file loaded" };
        }), "a call was refused");
        Require(queue.Enqueue([&log]() { log.Add("played"); }), "a call was refused");
        Require(log.WaitFor(2, std::chrono::seconds(5)), "the call after a failure did not run");
        Require(log.Entries() == std::vector<std::string>{ "failed", "played" }, "a failure changed the order");
    }

    void TestShutdownStopsAcceptingAndDiscardsPending()
    {
        Log log;
        MpvCallQueue queue;
        std::mutex gateMutex;
        std::condition_variable gate;
        bool release{};
        std::atomic_bool running{};
        // The first call blocks until released, so the next ones are still queued
        // when the shutdown begins.
        Require(queue.Enqueue([&running, &gateMutex, &gate, &release, &log]()
        {
            running.store(true);
            std::unique_lock lock{ gateMutex };
            gate.wait(lock, [&release]() { return release; });
            log.Add("running call finished");
        }), "a call was refused");
        auto const discardedOwner = std::make_shared<int>(7);
        std::weak_ptr<int> const discardedWatch = discardedOwner;
        Require(queue.Enqueue([&log, owner = discardedOwner]() { log.Add("pending call ran"); }), "a call was refused");
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!running.load() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Require(running.load(), "the first call never started");

        // Shut down from another thread while the call is still running: it must
        // wait for that call, then return with the pending one discarded.
        std::atomic_bool shutDown{};
        std::jthread closer([&queue, &shutDown]()
        {
            queue.Shutdown();
            shutDown.store(true);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        Require(!shutDown.load(), "shutdown returned while a call was still running");
        Require(!queue.Enqueue([&log]() { log.Add("late call ran"); }), "a call was accepted after shutdown began");
        {
            std::scoped_lock const lock{ gateMutex };
            release = true;
        }
        gate.notify_all();
        closer.join();
        Require(shutDown.load(), "shutdown did not return once the running call finished");
        Require(log.Entries() == std::vector<std::string>{ "running call finished" },
            "a call queued before shutdown, or one after it, still ran");
        Require(discardedWatch.use_count() == 1, "a discarded call's captures were kept alive");
        queue.Shutdown();
    }
}

void RunCallQueueTests()
{
    TestCallsRunOneAtATimeInOrder();
    TestFailedCallDoesNotHoldUpTheNext();
    TestShutdownStopsAcceptingAndDiscardsPending();
}
