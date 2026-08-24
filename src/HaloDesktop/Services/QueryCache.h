#pragma once

#include <any>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

namespace HaloDesktop::Services
{
    namespace QueryTtl
    {
        inline constexpr auto Forever = (std::chrono::milliseconds::max)();
        inline constexpr auto Addons = std::chrono::minutes{ 5 };
        inline constexpr auto Catalog = std::chrono::minutes{ 10 };
        inline constexpr auto Metadata = std::chrono::minutes{ 10 };
        inline constexpr auto Search = std::chrono::minutes{ 1 };
        inline constexpr auto Settings = std::chrono::minutes{ 1 };
        inline constexpr auto AlwaysRefetch = std::chrono::milliseconds{ 0 };
    }

    // UI-thread-only stale-while-revalidate snapshot cache. Every request
    // claims a monotonic sequence for its key; a late older response is
    // rejected even when it contains a complete server snapshot.
    class QueryCache final
    {
    public:
        using RequestId = std::uint64_t;

        QueryCache();

        [[nodiscard]] RequestId Issue(std::wstring_view key);
        [[nodiscard]] bool IsLatest(std::wstring_view key, RequestId requestId) const;

        template<typename T>
        [[nodiscard]] std::optional<T> TryGet(std::wstring_view key)
        {
            VerifyThread();
            auto const found = m_entries.find(std::wstring{ key });
            if (found == m_entries.end() || !found->second.Value.has_value())
            {
                return std::nullopt;
            }
            if (found->second.ExpiresAt != (Clock::time_point::max)()
                && Clock::now() >= found->second.ExpiresAt)
            {
                found->second.Value.reset();
                return std::nullopt;
            }
            auto const typed = std::any_cast<T>(&found->second.Value);
            return typed ? std::optional<T>{ *typed } : std::nullopt;
        }

        template<typename T>
        bool Commit(
            std::wstring_view key,
            RequestId requestId,
            T value,
            std::chrono::milliseconds timeToLive)
        {
            VerifyThread();
            auto const found = m_entries.find(std::wstring{ key });
            if (found == m_entries.end() || found->second.LatestIssued != requestId)
            {
                return false;
            }
            found->second.Value = std::move(value);
            auto const normalizedTimeToLive = timeToLive < std::chrono::milliseconds{ 0 }
                ? std::chrono::milliseconds{ 0 }
                : timeToLive;
            found->second.ExpiresAt = timeToLive == QueryTtl::Forever
                ? (Clock::time_point::max)()
                : Clock::now() + normalizedTimeToLive;
            return true;
        }

        void Invalidate(std::wstring_view key);
        void Clear();

    private:
        using Clock = std::chrono::steady_clock;

        struct Entry final
        {
            std::any Value;
            Clock::time_point ExpiresAt{};
            RequestId LatestIssued{};
        };

        void VerifyThread() const;

        std::thread::id m_ownerThread;
        RequestId m_nextRequestId{};
        std::unordered_map<std::wstring, Entry> m_entries;
    };
}
