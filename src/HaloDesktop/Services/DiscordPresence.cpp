#include "pch.h"
#include "Services/DiscordPresence.h"

#include "Config/DiscordConfig.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <windows.h>
#include <wil/resource.h>
#include <winrt/Windows.Data.Json.h>

namespace
{
    using HaloDesktop::Services::PresenceActivity;
    using HaloDesktop::Services::PresenceMedia;
    using HaloDesktop::Services::PresencePlaybackState;
    using JsonObject = winrt::Windows::Data::Json::JsonObject;
    using JsonValue = winrt::Windows::Data::Json::JsonValue;
    using JsonValueType = winrt::Windows::Data::Json::JsonValueType;

    constexpr std::size_t MaximumTextBytes = 128;
    constexpr std::size_t MaximumPipeAttempts = 10;
    constexpr DWORD PipeWaitMilliseconds = 100;
    constexpr auto PipeResponseTimeout = std::chrono::seconds{ 2 };

    winrt::hstring NextNonce(std::uint32_t processId)
    {
        static std::atomic_uint64_t sequence{};
        return winrt::to_hstring(processId) + L":" + winrt::to_hstring(++sequence);
    }

    std::wstring CleanText(std::wstring value)
    {
        std::wstring cleaned;
        cleaned.reserve(value.size());
        for (std::size_t index{}; index < value.size(); ++index)
        {
            auto character = value[index];
            if (character < 0x20 || character == 0x7f)
            {
                character = L' ';
            }
            if (character >= 0xd800 && character <= 0xdbff)
            {
                if (index + 1 < value.size() && value[index + 1] >= 0xdc00 && value[index + 1] <= 0xdfff)
                {
                    cleaned.push_back(character);
                    cleaned.push_back(value[++index]);
                    continue;
                }
                cleaned.push_back(0xfffd);
                continue;
            }
            if (character >= 0xdc00 && character <= 0xdfff)
            {
                cleaned.push_back(0xfffd);
                continue;
            }
            cleaned.push_back(character);
        }
        auto const first = cleaned.find_first_not_of(L' ');
        if (first == std::wstring::npos)
        {
            return {};
        }
        auto const last = cleaned.find_last_not_of(L' ');
        return cleaned.substr(first, last - first + 1);
    }

    std::wstring TruncateText(std::wstring value)
    {
        value = CleanText(std::move(value));
        while (!value.empty() && winrt::to_string(winrt::hstring{ value }).size() > MaximumTextBytes)
        {
            if (value.size() >= 2 && value.back() >= 0xdc00 && value.back() <= 0xdfff
                && value[value.size() - 2] >= 0xd800 && value[value.size() - 2] <= 0xdbff)
            {
                value.pop_back();
            }
            value.pop_back();
        }
        return value;
    }

    std::wstring JoinEpisode(PresenceMedia const& media)
    {
        auto const episode = CleanText(media.EpisodeLabel);
        auto const title = CleanText(media.Title);
        if (episode.empty())
        {
            return title;
        }
        if (title.empty())
        {
            return episode;
        }
        return episode + L" · " + title;
    }

    std::optional<PresencePlaybackState> PlaybackKind(
        ::HaloDesktop::Playback::PlaybackState const& state)
    {
        if (state.FileSerial == 0 || state.EndReason != ::HaloDesktop::Playback::PlaybackEndReason::None)
        {
            return std::nullopt;
        }
        if (state.Buffering)
        {
            return PresencePlaybackState::Buffering;
        }
        if (state.Paused)
        {
            return PresencePlaybackState::Paused;
        }
        return PresencePlaybackState::Playing;
    }

    std::int64_t UnixSeconds(std::chrono::system_clock::time_point value)
    {
        return std::chrono::duration_cast<std::chrono::seconds>(value.time_since_epoch()).count();
    }

    class NamedPipeTransport final : public HaloDesktop::Services::IDiscordPresenceTransport
    {
    public:
        [[nodiscard]] bool Send(
            std::wstring const& applicationId,
            std::string const& payload) noexcept override
        {
            try
            {
                if (!EnsureConnected(applicationId))
                {
                    return false;
                }
                auto const reply = Exchange(payload);
                if (reply == Reply::Success)
                {
                    return true;
                }
                if (reply == Reply::Error)
                {
                    auto fallback = WithoutArtwork(payload);
                    if (fallback && Exchange(*fallback) == Reply::Success)
                    {
                        return true;
                    }
                }
                m_pipe.reset();
                m_applicationId.clear();
            }
            catch (...)
            {
            }
            return false;
        }

    private:
        enum class Reply
        {
            Success,
            Error,
            Disconnected,
        };

        [[nodiscard]] bool EnsureConnected(std::wstring const& applicationId)
        {
            if (m_pipe && m_applicationId == applicationId)
            {
                return true;
            }
            m_pipe.reset();
            m_applicationId.clear();
            JsonObject handshake;
            handshake.Insert(L"v", JsonValue::CreateNumberValue(1));
            handshake.Insert(L"client_id", JsonValue::CreateStringValue(applicationId));
            auto const payload = winrt::to_string(handshake.Stringify());
            for (std::size_t index{}; index < MaximumPipeAttempts; ++index)
            {
                auto const pipeName = std::wstring{ L"\\\\.\\pipe\\discord-ipc-" } + std::to_wstring(index);
                if (!WaitNamedPipeW(pipeName.c_str(), PipeWaitMilliseconds))
                {
                    continue;
                }
                wil::unique_hfile candidate{ CreateFileW(
                    pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
                if (!candidate)
                {
                    continue;
                }
                m_pipe = std::move(candidate);
                if (WriteFrame(m_pipe.get(), 0, payload) && ReadReply() == Reply::Success)
                {
                    m_applicationId = applicationId;
                    return true;
                }
                m_pipe.reset();
            }
            return false;
        }

        [[nodiscard]] Reply Exchange(std::string const& payload) noexcept
        {
            return WriteFrame(m_pipe.get(), 1, payload)
                ? ReadReply()
                : Reply::Disconnected;
        }

        [[nodiscard]] static std::optional<std::string> WithoutArtwork(
            std::string const& payload) noexcept
        {
            try
            {
                auto root = JsonObject::Parse(winrt::to_hstring(payload));
                auto args = root.GetNamedObject(L"args");
                auto activity = args.GetNamedObject(L"activity");
                if (!activity.HasKey(L"assets"))
                {
                    return std::nullopt;
                }
                activity.Remove(L"assets");
                root.Insert(L"nonce", JsonValue::CreateStringValue(NextNonce(GetCurrentProcessId())));
                return winrt::to_string(root.Stringify());
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        [[nodiscard]] Reply ReadReply() noexcept
        {
            for (std::size_t attempt{}; attempt < 3; ++attempt)
            {
                std::uint32_t opcode{};
                std::string payload;
                if (!ReadFrame(m_pipe.get(), opcode, payload))
                {
                    return Reply::Disconnected;
                }
                if (opcode == 3)
                {
                    if (!WriteFrame(m_pipe.get(), 4, payload))
                    {
                        return Reply::Disconnected;
                    }
                    continue;
                }
                if (opcode != 1)
                {
                    return Reply::Disconnected;
                }
                try
                {
                    auto const response = JsonObject::Parse(winrt::to_hstring(payload));
                    if (!response.HasKey(L"evt"))
                    {
                        return Reply::Success;
                    }
                    auto const event = response.GetNamedValue(L"evt");
                    if (event.ValueType() == JsonValueType::Null)
                    {
                        return Reply::Success;
                    }
                    return event.ValueType() == JsonValueType::String
                        && event.GetString() == L"ERROR"
                        ? Reply::Error
                        : Reply::Success;
                }
                catch (...)
                {
                    return Reply::Disconnected;
                }
            }
            return Reply::Disconnected;
        }

        [[nodiscard]] static bool WaitForBytes(HANDLE pipe, DWORD wanted) noexcept
        {
            auto const deadline = std::chrono::steady_clock::now() + PipeResponseTimeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                DWORD available{};
                if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
                {
                    return false;
                }
                if (available >= wanted)
                {
                    return true;
                }
                Sleep(10);
            }
            return false;
        }

        [[nodiscard]] static bool ReadFrame(
            HANDLE pipe,
            std::uint32_t& opcode,
            std::string& payload) noexcept
        {
            std::uint32_t header[2]{};
            DWORD read{};
            if (!WaitForBytes(pipe, sizeof(header))
                || !ReadFile(pipe, header, sizeof(header), &read, nullptr)
                || read != sizeof(header)
                || header[1] > 1024u * 1024u)
            {
                return false;
            }
            opcode = header[0];
            payload.resize(header[1]);
            if (payload.empty())
            {
                return true;
            }
            return WaitForBytes(pipe, header[1])
                && ReadFile(pipe, payload.data(), header[1], &read, nullptr)
                && read == header[1];
        }

        [[nodiscard]] static bool WriteFrame(
            HANDLE pipe,
            std::uint32_t opcode,
            std::string const& payload) noexcept
        {
            std::uint32_t header[2]{ opcode, static_cast<std::uint32_t>(payload.size()) };
            DWORD written{};
            if (!WriteFile(pipe, header, sizeof(header), &written, nullptr)
                || written != sizeof(header))
            {
                return false;
            }
            if (payload.empty())
            {
                return true;
            }
            return WriteFile(
                pipe,
                payload.data(),
                static_cast<DWORD>(payload.size()),
                &written,
                nullptr) != FALSE
                && written == static_cast<DWORD>(payload.size());
        }

        wil::unique_hfile m_pipe;
        std::wstring m_applicationId;
    };

    std::shared_ptr<HaloDesktop::Services::IDiscordPresenceTransport> DefaultTransport()
    {
        return std::make_shared<NamedPipeTransport>();
    }
}

namespace HaloDesktop::Services
{
    std::optional<PresenceActivity> BuildPresenceActivity(
        PresenceMedia const& media,
        ::HaloDesktop::Playback::PlaybackState const& state,
        std::chrono::system_clock::time_point now)
    {
        auto const kind = PlaybackKind(state);
        if (!kind)
        {
            return std::nullopt;
        }
        auto details = CleanText(media.ShowName);
        auto episode = JoinEpisode(media);
        if (details.empty())
        {
            details = CleanText(media.Title);
            episode.clear();
        }
        details = TruncateText(std::move(details));
        episode = TruncateText(std::move(episode));
        if (details.empty())
        {
            return std::nullopt;
        }
        if (episode.empty() && CleanText(media.ShowName).empty())
        {
            episode = L"Movie";
        }
        if (*kind == PresencePlaybackState::Paused)
        {
            episode = episode.empty() ? L"Paused" : episode + L" · Paused";
        }
        else if (*kind == PresencePlaybackState::Buffering)
        {
            episode = episode.empty() ? L"Buffering" : episode + L" · Buffering";
        }
        episode = TruncateText(std::move(episode));
        return PresenceActivity{
            .Details = std::move(details),
            .State = std::move(episode),
            .Playback = *kind,
            .PositionSeconds = std::isfinite(state.PositionSeconds) && state.PositionSeconds > 0.0 ? state.PositionSeconds : 0.0,
            .DurationSeconds = std::isfinite(state.DurationSeconds) && state.DurationSeconds > 0.0 ? state.DurationSeconds : 0.0,
            .PlaybackRate = std::isfinite(state.Speed) && state.Speed > 0.0 ? state.Speed : 1.0,
            .FileSerial = state.FileSerial,
            .SeekSerial = state.SeekSerial,
            .CapturedAt = now,
        };
    }

    std::string SerializeSetActivity(PresenceActivity const& activity, std::uint32_t processId)
    {
        JsonObject root;
        root.Insert(L"cmd", JsonValue::CreateStringValue(L"SET_ACTIVITY"));
        root.Insert(L"nonce", JsonValue::CreateStringValue(NextNonce(processId)));
        JsonObject args;
        args.Insert(L"pid", JsonValue::CreateNumberValue(processId));
        JsonObject presence;
        presence.Insert(L"details", JsonValue::CreateStringValue(activity.Details));
        if (!activity.State.empty())
        {
            presence.Insert(L"state", JsonValue::CreateStringValue(activity.State));
        }
        presence.Insert(L"assets", []
        {
            JsonObject assets;
            assets.Insert(L"large_image", JsonValue::CreateStringValue(Config::DiscordArtworkKey));
            assets.Insert(L"large_text", JsonValue::CreateStringValue(L"Halo"));
            return assets;
        }());
        presence.Insert(L"instance", JsonValue::CreateBooleanValue(false));
        // Discord activity type 3 is Watching. Omitting this defaults to Playing.
        presence.Insert(L"type", JsonValue::CreateNumberValue(3));
        if (activity.Playback == PresencePlaybackState::Playing)
        {
            JsonObject timestamps;
            auto const start = UnixSeconds(activity.CapturedAt)
                - static_cast<std::int64_t>(activity.PositionSeconds / activity.PlaybackRate);
            timestamps.Insert(L"start", JsonValue::CreateNumberValue(static_cast<double>(start)));
            if (activity.DurationSeconds > activity.PositionSeconds)
            {
                auto const end = UnixSeconds(activity.CapturedAt)
                    + static_cast<std::int64_t>((activity.DurationSeconds - activity.PositionSeconds)
                        / activity.PlaybackRate);
                timestamps.Insert(L"end", JsonValue::CreateNumberValue(static_cast<double>(end)));
            }
            presence.Insert(L"timestamps", timestamps);
        }
        args.Insert(L"activity", presence);
        root.Insert(L"args", args);
        return winrt::to_string(root.Stringify());
    }

    std::string SerializeClearActivity(std::uint32_t processId)
    {
        JsonObject root;
        root.Insert(L"cmd", JsonValue::CreateStringValue(L"SET_ACTIVITY"));
        root.Insert(L"nonce", JsonValue::CreateStringValue(NextNonce(processId)));
        JsonObject args;
        args.Insert(L"pid", JsonValue::CreateNumberValue(processId));
        args.Insert(L"activity", JsonValue::CreateNullValue());
        root.Insert(L"args", args);
        return winrt::to_string(root.Stringify());
    }

    DiscordPresenceService::DiscordPresenceService(
        bool enabled,
        std::shared_ptr<IDiscordPresenceTransport> transport,
        std::chrono::milliseconds retryDelay)
        : m_transport(transport ? std::move(transport) : DefaultTransport()),
          m_enabled(enabled),
          m_retryDelay((std::max)(retryDelay, std::chrono::milliseconds{ 1 }))
    {
        m_worker = std::jthread([this](std::stop_token stopToken) { Worker(stopToken); });
    }

    DiscordPresenceService::~DiscordPresenceService()
    {
        Clear();
        {
            std::scoped_lock const lock{ m_mutex };
            m_stopping = true;
        }
        m_wake.notify_all();
    }

    void DiscordPresenceService::SetEnabled(bool enabled) noexcept
    {
        try
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_enabled == enabled)
            {
                return;
            }
            m_enabled = enabled;
            if (!enabled)
            {
                auto const shouldClear = m_hasPublished || m_lastActivity.has_value() || m_pending.has_value();
                m_pending.reset();
                if (shouldClear)
                {
                    m_pending = Pending{ .Clear = true };
                }
            }
            else
            {
                auto activity = std::move(m_lastActivity);
                m_lastActivity.reset();
                m_pending = activity
                    ? std::optional<Pending>{ Pending{ .Activity = std::move(activity) } }
                    : std::nullopt;
            }
            m_wake.notify_all();
        }
        catch (...)
        {
        }
    }

    bool DiscordPresenceService::Enabled() const noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        return m_enabled;
    }

    void DiscordPresenceService::SetMedia(PresenceMedia media) noexcept
    {
        try
        {
            std::scoped_lock const lock{ m_mutex };
            m_media = std::move(media);
            m_lastActivity.reset();
            m_pending.reset();
        }
        catch (...)
        {
        }
    }

    void DiscordPresenceService::Update(
        ::HaloDesktop::Playback::PlaybackState const& state) noexcept
    {
        try
        {
            std::optional<PresenceActivity> activity;
            {
                std::scoped_lock const lock{ m_mutex };
                if (!m_media)
                {
                    return;
                }
                activity = BuildPresenceActivity(*m_media, state);
            }
            if (activity)
            {
                QueueActivity(std::move(*activity));
            }
            else
            {
                QueueClearPreservingMedia();
            }
        }
        catch (...)
        {
        }
    }

    void DiscordPresenceService::Clear() noexcept
    {
        try
        {
            std::scoped_lock const lock{ m_mutex };
            auto const shouldClear = m_hasPublished || m_lastActivity.has_value() || m_pending.has_value();
            m_media.reset();
            m_lastActivity.reset();
            m_pending.reset();
            if (shouldClear)
            {
                m_pending = Pending{ .Clear = true };
            }
            m_wake.notify_all();
        }
        catch (...)
        {
        }
    }

    void DiscordPresenceService::QueueActivity(PresenceActivity activity) noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        if (m_lastActivity && SameActivityIdentity(*m_lastActivity, activity))
        {
            auto const elapsed = activity.CapturedAt - m_lastActivity->CapturedAt;
            if (activity.Playback != PresencePlaybackState::Playing
                || elapsed < std::chrono::seconds{ 15 })
            {
                return;
            }
        }
        m_lastActivity = activity;
        if (!m_enabled)
        {
            return;
        }
        m_pending = Pending{ .Activity = std::move(activity) };
        m_wake.notify_all();
    }

    void DiscordPresenceService::QueueClearPreservingMedia() noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        auto const shouldClear = m_hasPublished || m_lastActivity.has_value() || m_pending.has_value();
        m_lastActivity.reset();
        m_pending.reset();
        if (shouldClear)
        {
            m_pending = Pending{ .Clear = true };
            m_wake.notify_all();
        }
    }

    void DiscordPresenceService::Worker(std::stop_token stopToken)
    {
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        }
        catch (...)
        {
            return;
        }
        auto const uninitialize = wil::scope_exit([]
        {
            winrt::uninit_apartment();
        });
        while (true)
        {
            Pending pending;
            {
                std::unique_lock lock{ m_mutex };
                m_wake.wait(lock, stopToken, [this] { return m_stopping || m_pending.has_value(); });
                if (!m_pending)
                {
                    return;
                }
                pending = std::move(*m_pending);
                m_pending.reset();
            }
            auto const payload = pending.Clear
                ? SerializeClearActivity(GetCurrentProcessId())
                : SerializeSetActivity(*pending.Activity, GetCurrentProcessId());
            auto const sent = m_transport->Send(Config::DiscordApplicationId, payload);
            {
                std::scoped_lock const lock{ m_mutex };
                if (sent)
                {
                    m_hasPublished = !pending.Clear;
                    continue;
                }
                if (m_stopping)
                {
                    return;
                }
            }
            std::unique_lock lock{ m_mutex };
            m_wake.wait_for(lock, stopToken, m_retryDelay, [this]
            {
                return m_stopping || m_pending.has_value();
            });
            if (!m_stopping && !m_pending)
            {
                m_pending = std::move(pending);
                m_wake.notify_all();
            }
        }
    }

    bool DiscordPresenceService::SameActivityIdentity(
        PresenceActivity const& left,
        PresenceActivity const& right) noexcept
    {
        return left.Details == right.Details
            && left.State == right.State
            && left.Playback == right.Playback
            && left.FileSerial == right.FileSerial
            && left.SeekSerial == right.SeekSerial
            && std::abs(left.PlaybackRate - right.PlaybackRate) < 0.001;
    }
}
