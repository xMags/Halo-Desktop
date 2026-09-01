#pragma once

#include "Playback/IPlaybackEngine.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

namespace HaloDesktop::Services
{
    struct PresenceMedia final
    {
        std::wstring Title;
        std::wstring ShowName;
        std::wstring EpisodeLabel;
        std::wstring PosterUrl;
    };

    enum class PresencePlaybackState
    {
        Playing,
        Paused,
        Buffering,
    };

    struct PresenceActivity final
    {
        std::wstring Details;
        std::wstring State;
        std::wstring ArtworkUrl;
        PresencePlaybackState Playback{ PresencePlaybackState::Playing };
        double PositionSeconds{};
        double DurationSeconds{};
        double PlaybackRate{ 1.0 };
        std::uint64_t FileSerial{};
        std::uint64_t SeekSerial{};
        std::chrono::system_clock::time_point CapturedAt{};
    };

    [[nodiscard]] std::optional<PresenceActivity> BuildPresenceActivity(
        PresenceMedia const& media,
        ::HaloDesktop::Playback::PlaybackState const& state,
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

    [[nodiscard]] std::string SerializeSetActivity(
        PresenceActivity const& activity,
        std::uint32_t processId);

    [[nodiscard]] std::string SerializeClearActivity(std::uint32_t processId);

    class IDiscordPresenceTransport
    {
    public:
        virtual ~IDiscordPresenceTransport() = default;
        [[nodiscard]] virtual bool Send(
            std::wstring const& applicationId,
            std::string const& payload) noexcept = 0;
    };

    class DiscordPresenceService final
    {
    public:
        explicit DiscordPresenceService(
            bool enabled = true,
            std::shared_ptr<IDiscordPresenceTransport> transport = nullptr,
            std::chrono::milliseconds retryDelay = std::chrono::seconds{ 5 });
        ~DiscordPresenceService();

        DiscordPresenceService(DiscordPresenceService const&) = delete;
        DiscordPresenceService& operator=(DiscordPresenceService const&) = delete;

        void SetEnabled(bool enabled) noexcept;
        [[nodiscard]] bool Enabled() const noexcept;
        void SetMedia(PresenceMedia media) noexcept;
        void Update(::HaloDesktop::Playback::PlaybackState const& state) noexcept;
        void Clear() noexcept;

    private:
        struct Pending final
        {
            std::optional<PresenceActivity> Activity;
            bool Clear{};
        };

        void QueueActivity(PresenceActivity activity) noexcept;
        void QueueClearPreservingMedia() noexcept;
        void Worker(std::stop_token stopToken);
        [[nodiscard]] static bool SameActivityIdentity(
            PresenceActivity const& left,
            PresenceActivity const& right) noexcept;

        std::shared_ptr<IDiscordPresenceTransport> m_transport;
        mutable std::mutex m_mutex;
        std::condition_variable_any m_wake;
        std::optional<PresenceMedia> m_media;
        std::optional<PresenceActivity> m_lastActivity;
        std::optional<Pending> m_pending;
        bool m_enabled{};
        bool m_hasPublished{};
        bool m_stopping{};
        std::chrono::milliseconds m_retryDelay;
        std::jthread m_worker;
    };
}
