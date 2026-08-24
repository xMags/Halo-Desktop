#include "pch.h"
#include "Playback/MpvEngine.h"

#include "Playback/MpvClient.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <cwctype>

namespace
{
    // Upper bound on how long the engine trusts a commanded position without a
    // restart from libmpv, so a dropped or rejected seek cannot freeze the playhead.
    constexpr std::chrono::milliseconds SeekSettleTimeout{ 3000 };
    constexpr double SeekTargetToleranceSeconds{ 1.0 };
} // namespace

namespace HaloDesktop::Playback
{
    MpvEngine::MpvEngine() = default;

    MpvEngine::~MpvEngine()
    {
        Stop();
    }

    void MpvEngine::Start()
    {
        if (m_running)
        {
            return;
        }
        if (m_windowHandle == 0)
        {
            throw std::logic_error("A video host window must be attached before playback starts");
        }

        auto const dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        if (!dispatcher)
        {
            throw winrt::hresult_wrong_thread();
        }

        auto const weak = weak_from_this();
        m_client = std::make_unique<MpvClient>(m_windowHandle, dispatcher, [weak](PlaybackUpdate update) {
            if (auto const self = weak.lock())
            {
                self->ApplyUpdate(std::move(update));
            }
        });
        m_running = true;
        try
        {
            m_client->SetVolume(m_state.Volume);
            m_client->SetSpeed(m_state.Speed);
            if (m_state.Paused)
            {
                m_client->SetPaused(true);
            }
            if (!m_source.empty())
            {
                m_client->Open(m_source);
            }
        }
        catch (...)
        {
            Stop();
            throw;
        }
    }

    void MpvEngine::Stop() noexcept
    {
        m_running = false;
        if (m_client)
        {
            m_client->Shutdown();
            m_client.reset();
        }
        m_source.clear();
        m_seekTarget.reset();
        m_seekRestarted = false;
        m_state.PositionSeconds = 0.0;
        m_state.DurationSeconds = 0.0;
        m_state.Paused = false;
        m_state.Buffering = false;
        m_state.Tracks.clear();
    }

    void MpvEngine::Open(std::wstring const& source)
    {
        auto lower=source;std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t value){return static_cast<wchar_t>(std::towlower(value));});
        auto const remote=lower.starts_with(L"http://")||lower.starts_with(L"https://");
        std::error_code error;auto const path=std::filesystem::path(source);
        if (source.empty() || (!remote && (!std::filesystem::is_regular_file(path,error)||error)))
        {
            throw std::invalid_argument("Playback source must be an HTTP URL or an existing local file");
        }

        m_source = remote?source:path.wstring();
        m_seekTarget.reset();
        m_seekRestarted = false;
        m_state.PositionSeconds = 0.0;
        m_state.DurationSeconds = 0.0;
        m_state.Buffering = true;
        m_state.EndReason = PlaybackEndReason::None;
        m_state.Tracks.clear();
        if (m_client)
        {
            m_client->Open(m_source);
        }
        NotifyChanged();
    }

    void MpvEngine::AttachVideoWindow(std::uintptr_t windowHandle)
    {
        if (windowHandle == 0)
        {
            throw std::invalid_argument("A valid child window handle is required");
        }
        if (m_windowHandle == windowHandle)
        {
            return;
        }

        auto const restart = m_running;
        if (restart)
        {
            Stop();
        }
        m_windowHandle = windowHandle;
        if (restart)
        {
            Start();
        }
    }

    void MpvEngine::DetachVideoWindow() noexcept
    {
        Stop();
        m_windowHandle = 0;
    }

    void MpvEngine::SetPaused(bool paused)
    {
        if (m_client)
        {
            m_client->SetPaused(paused);
        }
        if (m_state.Paused != paused)
        {
            m_state.Paused = paused;
            NotifyChanged();
        }
    }

    void MpvEngine::SeekAbsolute(double seconds)
    {
        auto const next = std::clamp(seconds, 0.0, (std::max)(DurationNow(), m_state.DurationSeconds));
        if (m_client)
        {
            m_seekTarget.reset();
            m_seekRestarted = false;
            m_client->SeekAbsolute(next);
            BeginSeek(next);
        }
        if (std::abs(next - m_state.PositionSeconds) >= 0.001)
        {
            m_state.PositionSeconds = next;
            NotifyChanged();
        }
    }

    void MpvEngine::SeekRelative(double seconds)
    {
        auto const next = std::clamp(m_state.PositionSeconds + seconds, 0.0, (std::max)(m_state.DurationSeconds, 0.0));
        if (m_client)
        {
            m_seekTarget.reset();
            m_seekRestarted = false;
            m_client->SeekRelative(seconds);
            BeginSeek(next);
        }
        if (std::abs(next - m_state.PositionSeconds) >= 0.001)
        {
            m_state.PositionSeconds = next;
            NotifyChanged();
        }
    }

    void MpvEngine::SetVolume(double volume)
    {
        auto const next = std::clamp(volume, 0.0, 100.0);
        if (m_client)
        {
            m_client->SetVolume(next);
        }
        if (std::abs(next - m_state.Volume) >= 0.001)
        {
            m_state.Volume = next;
            NotifyChanged();
        }
    }

    void MpvEngine::SetSpeed(double speed)
    {
        auto const next = std::clamp(speed, 0.5, 2.0);
        if (m_client)
        {
            m_client->SetSpeed(next);
        }
        if (std::abs(next - m_state.Speed) >= 0.001)
        {
            m_state.Speed = next;
            NotifyChanged();
        }
    }

    void MpvEngine::SetAudioTrack(std::int64_t id)
    {
        if (m_client)
        {
            m_client->SetAudioTrack(id);
        }
        for (auto& track : m_state.Tracks)
        {
            if (track.Type == TrackType::Audio)
            {
                track.Selected = track.Id == id;
            }
        }
        NotifyChanged();
    }

    void MpvEngine::SetSubtitleTrack(std::optional<std::int64_t> id)
    {
        if (m_client)
        {
            m_client->SetSubtitleTrack(id);
        }
        for (auto& track : m_state.Tracks)
        {
            if (track.Type == TrackType::Subtitle)
            {
                track.Selected = id && track.Id == *id;
            }
        }
        NotifyChanged();
    }

    void MpvEngine::SetSubtitleDelay(double seconds)
    {
        if (m_client)
        {
            m_client->SetSubtitleDelay(seconds);
        }
    }

    void MpvEngine::SetAudioDelay(double seconds)
    {
        if (m_client)
        {
            m_client->SetAudioDelay(seconds);
        }
    }

    PlaybackState MpvEngine::State() const
    {
        return m_state;
    }
    double MpvEngine::DurationNow()const noexcept{return m_client?m_client->DurationSeconds():m_state.DurationSeconds;}

    PlaybackChangedToken MpvEngine::AddChangedHandler(PlaybackChangedHandler handler)
    {
        if (!handler)
        {
            return 0;
        }
        auto const token = ++m_nextToken;
        m_handlers.emplace(token, std::move(handler));
        return token;
    }

    void MpvEngine::RemoveChangedHandler(PlaybackChangedToken token) noexcept
    {
        m_handlers.erase(token);
    }

    void MpvEngine::ApplyUpdate(PlaybackUpdate update)
    {
        if (update.Seeking)
        {
            if (*update.Seeking)
            {
                if (m_seekTarget)
                {
                    m_seekRestarted = false;
                }
            }
            else if (m_seekTarget)
            {
                m_seekRestarted = true;
            }
        }
        if (update.PositionSeconds && AcceptPosition(*update.PositionSeconds))
        {
            m_state.PositionSeconds = *update.PositionSeconds;
        }
        if (update.DurationSeconds)
        {
            m_state.DurationSeconds = *update.DurationSeconds;
        }
        if (update.Volume)
        {
            m_state.Volume = *update.Volume;
        }
        if (update.Speed)
        {
            m_state.Speed = *update.Speed;
        }
        if (update.Paused)
        {
            m_state.Paused = *update.Paused;
        }
        if (update.Buffering)
        {
            m_state.Buffering = *update.Buffering;
        }
        if (update.Ended && *update.Ended)
        {
            m_state.Paused = true;
        }
        if (update.Tracks)
        {
            m_state.Tracks = std::move(*update.Tracks);
        }
        if(update.FileLoaded&&*update.FileLoaded){++m_state.FileSerial;m_state.EndReason=PlaybackEndReason::None;}
        if(update.EndReason){m_state.EndReason=*update.EndReason;++m_state.EndSerial;if(*update.EndReason==PlaybackEndReason::Error)m_state.Buffering=false;}
        NotifyChanged();
    }

    void MpvEngine::BeginSeek(double targetSeconds) noexcept
    {
        m_seekTarget = targetSeconds;
        m_seekRestarted = false;
        m_seekIssuedAt = std::chrono::steady_clock::now();
    }

    bool MpvEngine::AcceptPosition(double positionSeconds) noexcept
    {
        if (!m_seekTarget)
        {
            return true;
        }
        if (std::chrono::steady_clock::now() - m_seekIssuedAt > SeekSettleTimeout)
        {
            m_seekTarget.reset();
            m_seekRestarted = false;
            return true;
        }
        if (!m_seekRestarted || std::abs(positionSeconds - *m_seekTarget) > SeekTargetToleranceSeconds)
        {
            return false;
        }

        m_seekTarget.reset();
        m_seekRestarted = false;
        return true;
    }

    void MpvEngine::NotifyChanged()
    {
        std::vector<PlaybackChangedHandler> handlers;
        handlers.reserve(m_handlers.size());
        for (auto const& [token, handler] : m_handlers)
        {
            static_cast<void>(token);
            handlers.push_back(handler);
        }
        for (auto const& handler : handlers)
        {
            try{handler();}catch(...){}
        }
    }
} // namespace HaloDesktop::Playback
