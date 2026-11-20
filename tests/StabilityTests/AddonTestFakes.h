#pragma once

// Fakes shared by the addon resolution and device backend tests: a fetch that
// answers from a table of exact URLs (anything else is a 404) and counts every
// request, and an in-memory document store, as the reference tests' fakeFetch
// and MemoryStore are. Nothing here touches the network or the disk.

#include "Api/Addons/AddonFetcher.h"
#include "Services/Device/DeviceStore.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

namespace AddonTests
{
    inline void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    struct Route final
    {
        enum class Kind
        {
            Answer,
            // Never answers: settles only when the request's budget runs out
            // or its caller cancels, as a real hanging fetch does.
            Hang,
        };

        Kind Behaviour{ Kind::Answer };
        std::uint16_t Status{ 200 };
        std::wstring Body;
    };

    inline Route Json(std::wstring body)
    {
        return { Route::Kind::Answer, 200, std::move(body) };
    }

    inline Route Status(std::uint16_t status, std::wstring body = L"")
    {
        return { Route::Kind::Answer, status, std::move(body) };
    }

    inline Route Hang()
    {
        return { Route::Kind::Hang, 0, {} };
    }

    class FakeFetcher final : public HaloDesktop::Api::Addons::IAddonFetcher
    {
    public:
        explicit FakeFetcher(std::map<std::wstring, Route> routes)
            : m_routes(std::move(routes))
        {
        }

        void Set(std::wstring const& url, Route route)
        {
            std::scoped_lock const lock{ m_mutex };
            m_routes.insert_or_assign(url, std::move(route));
        }

        [[nodiscard]] std::vector<std::wstring> Calls() const
        {
            std::scoped_lock const lock{ m_mutex };
            return m_calls;
        }

        void ClearCalls()
        {
            std::scoped_lock const lock{ m_mutex };
            m_calls.clear();
        }

        [[nodiscard]] concurrency::task<HaloDesktop::Api::Addons::AddonFetchResponse> GetAsync(
            std::wstring url,
            std::chrono::milliseconds budget,
            concurrency::cancellation_token cancellation) override
        {
            Route route{ Route::Kind::Answer, 404, L"not found" };
            {
                std::scoped_lock const lock{ m_mutex };
                m_calls.push_back(url);
                if (auto const found = m_routes.find(url); found != m_routes.end()) route = found->second;
            }
            if (route.Behaviour == Route::Kind::Answer)
            {
                auto const ok = route.Status >= 200 && route.Status <= 299;
                co_return HaloDesktop::Api::Addons::AddonFetchResponse{
                    route.Status, ok ? winrt::hstring{ route.Body } : winrt::hstring{} };
            }
            co_await winrt::resume_background();
            auto const started = std::chrono::steady_clock::now();
            for (;;)
            {
                if (cancellation.is_canceled()) throw winrt::hresult_canceled{};
                if (std::chrono::steady_clock::now() - started >= budget)
                {
                    throw HaloDesktop::Api::Addons::AddonRequestFailure{
                        { HaloDesktop::Api::Addons::AddonFailureKind::Timeout, std::nullopt } };
                }
                Sleep(2);
            }
        }

    private:
        mutable std::mutex m_mutex;
        std::map<std::wstring, Route> m_routes;
        std::vector<std::wstring> m_calls;
    };

    class MemoryStore final : public HaloDesktop::Services::Device::IDeviceDocumentStore
    {
    public:
        // Delay before each read answers, to make overlapping writes interleave.
        std::atomic_int ReadDelayMs{};

        [[nodiscard]] concurrency::task<std::optional<std::string>> ReadAsync(
            HaloDesktop::Services::Device::DeviceCollection collection) override
        {
            std::optional<std::string> value;
            {
                std::scoped_lock const lock{ m_mutex };
                if (auto const found = m_documents.find(collection); found != m_documents.end()) value = found->second;
            }
            if (auto const delay = ReadDelayMs.load(); delay > 0)
            {
                co_await winrt::resume_background();
                Sleep(static_cast<DWORD>(delay));
            }
            co_return value;
        }

        [[nodiscard]] concurrency::task<void> WriteAsync(
            HaloDesktop::Services::Device::DeviceCollection collection,
            std::string contents) override
        {
            {
                std::scoped_lock const lock{ m_mutex };
                m_documents.insert_or_assign(collection, std::move(contents));
            }
            co_return;
        }

        // Runs at the start of each update, before the update reads: a test's
        // stand-in for another Halo process writing after this one last read.
        std::function<void(MemoryStore&, HaloDesktop::Services::Device::DeviceCollection)> BeforeUpdate;
        // Runs once an update has written, or chosen not to.
        std::function<void(MemoryStore&, HaloDesktop::Services::Device::DeviceCollection)> AfterUpdate;

        [[nodiscard]] concurrency::task<void> UpdateAsync(
            HaloDesktop::Services::Device::DeviceCollection collection,
            DocumentTransform transform) override
        {
            if (BeforeUpdate) BeforeUpdate(*this, collection);
            {
                std::scoped_lock const lock{ m_mutex };
                std::optional<std::string> current;
                if (auto const found = m_documents.find(collection); found != m_documents.end()) current = found->second;
                if (auto next = transform(current)) m_documents.insert_or_assign(collection, std::move(*next));
            }
            if (AfterUpdate) AfterUpdate(*this, collection);
            co_return;
        }

        void Set(HaloDesktop::Services::Device::DeviceCollection collection, std::string contents)
        {
            std::scoped_lock const lock{ m_mutex };
            m_documents.insert_or_assign(collection, std::move(contents));
        }

        [[nodiscard]] std::optional<std::string> Get(HaloDesktop::Services::Device::DeviceCollection collection) const
        {
            std::scoped_lock const lock{ m_mutex };
            if (auto const found = m_documents.find(collection); found != m_documents.end()) return found->second;
            return std::nullopt;
        }

        [[nodiscard]] std::size_t Size() const
        {
            std::scoped_lock const lock{ m_mutex };
            return m_documents.size();
        }

    private:
        mutable std::mutex m_mutex;
        std::map<HaloDesktop::Services::Device::DeviceCollection, std::string> m_documents;
    };
}
