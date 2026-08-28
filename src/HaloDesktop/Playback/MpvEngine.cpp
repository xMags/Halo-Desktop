#include "pch.h"
#include "Playback/MpvEngine.h"

#include "Playback/MpvClient.h"
#include "Playback/PlaybackPolicy.h"
#include "Playback/WindowsAudioSession.h"
#include "Security/ProtectedHttpHeaders.h"
#include "Services/PlaybackPreferences.h"

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
    MpvEngine::MpvEngine(std::shared_ptr<::HaloDesktop::Services::PlaybackPreferences> preferences)
        : m_preferences(std::move(preferences))
    {
        if (!m_preferences)
        {
            throw std::invalid_argument{ "MpvEngine requires playback preferences." };
        }
    }

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
        }, m_preferences->HardwareDecodingEnabled());
        m_running = true;
        try
        {
            m_client->SetVolume(m_state.Volume);
            m_client->SetSpeed(m_state.Speed);
            m_client->ApplySubtitleStyle(m_subtitleStyle);
            if (m_state.Paused)
            {
                m_client->SetPaused(true);
            }
            if (!m_source.Location.empty())
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
        m_source = {};
        m_seekTarget.reset();
        m_seekRestarted = false;
        m_state.PositionSeconds = 0.0;
        m_state.DurationSeconds = 0.0;
        m_state.Paused = false;
        m_state.Buffering = false;
        m_state.SeekPending = false;
        m_state.FirstFrameReady = false;
        m_pausedForCache = false;
        m_state.TracksReady = false;
        m_state.Tracks.clear();
        m_state.Video.reset();
        m_audioSessionSerial = 0;
    }

    void MpvEngine::Open(PlaybackSource source)
    {
        auto lower=source.Location;std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t value){return static_cast<wchar_t>(std::towlower(value));});
        auto const remote=lower.starts_with(L"http://")||lower.starts_with(L"https://");
        std::error_code error;auto const path=std::filesystem::path(source.Location);
        if (source.Location.empty() || (!remote && (!std::filesystem::is_regular_file(path,error)||error)))
        {
            throw std::invalid_argument("Playback source must be an HTTP URL or an existing local file");
        }
        if (!remote && !source.Headers.empty())
        {
            throw std::invalid_argument("Local playback sources cannot include HTTP request headers");
        }
        Security::ValidateProtectedHttpHeaders(source.Headers);

        source.Location = remote ? std::move(source.Location) : path.wstring();
        m_source = std::move(source);
        m_seekTarget.reset();
        m_seekRestarted = false;
        m_state.PositionSeconds = 0.0;
        m_state.DurationSeconds = 0.0;
        m_state.Buffering = true;
        m_state.SeekPending = false;
        m_state.FirstFrameReady = false;
        m_pausedForCache = false;
        m_state.TracksReady = false;
        m_state.EndReason = PlaybackEndReason::None;
        m_state.Tracks.clear();
        // libmpv clears video-params by reporting no value at all, and an empty
        // property change carries nothing to translate, so the previous file's
        // format has to be dropped here or it would describe the next one.
        m_state.Video.reset();
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
        if (!std::isfinite(seconds))
        {
            return;
        }

        auto liveDuration = DurationNow();
        if (!std::isfinite(liveDuration) || liveDuration < 0.0)
        {
            liveDuration = 0.0;
        }
        auto const next = std::clamp(
            seconds,
            0.0,
            (std::max)(liveDuration, m_state.DurationSeconds));
        if (m_client)
        {
            m_seekTarget.reset();
            m_seekRestarted = false;
            try
            {
                m_client->SeekAbsolute(next);
            }
            catch (std::runtime_error const&)
            {
                return;
            }
            BeginSeek(next);
        }
        ++m_state.SeekSerial;
        if (std::abs(next - m_state.PositionSeconds) >= 0.001)
        {
            m_state.PositionSeconds = next;
            NotifyChanged();
        }
    }

    void MpvEngine::SeekRelative(double seconds)
    {
        if (!std::isfinite(seconds))
        {
            return;
        }

        auto const next = std::clamp(m_state.PositionSeconds + seconds, 0.0, (std::max)(m_state.DurationSeconds, 0.0));
        if (m_client)
        {
            m_seekTarget.reset();
            m_seekRestarted = false;
            try
            {
                m_client->SeekRelative(seconds);
            }
            catch (std::runtime_error const&)
            {
                return;
            }
            BeginSeek(next);
        }
        ++m_state.SeekSerial;
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
            auto const sessionApplied = WindowsAudioSession::SetVolume(next / 100.0, next > 0.0);
            m_client->SetVolume(sessionApplied ? 100.0 : next);
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
        ++m_state.AudioSelectionSerial;
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
        ++m_state.SubtitleSelectionSerial;
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
    void MpvEngine::AddExternalSubtitle(std::wstring const&path,std::wstring const&identity,std::wstring const&displayTitle,std::wstring const&language){++m_state.SubtitleSelectionSerial;if(m_client)m_client->AddExternalSubtitle(path,identity,displayTitle,language);}
    void MpvEngine::RemoveTrack(std::int64_t id){if(m_client)m_client->RemoveTrack(id);}
    void MpvEngine::ApplySubtitleStyle(SubtitleStyle const&style){m_subtitleStyle=style;if(m_client)m_client->ApplySubtitleStyle(style);}
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
            m_state.SeekPending = *update.Seeking;
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
        if (update.PositionSeconds
            && std::isfinite(*update.PositionSeconds)
            && AcceptPosition(*update.PositionSeconds))
        {
            m_state.PositionSeconds = *update.PositionSeconds;
        }
        if (update.DurationSeconds
            && std::isfinite(*update.DurationSeconds)
            && *update.DurationSeconds >= 0.0)
        {
            m_state.DurationSeconds = *update.DurationSeconds;
        }
        auto const timeline = NormalizePlaybackTimeline(
            m_state.PositionSeconds,
            m_state.DurationSeconds);
        m_state.PositionSeconds = timeline.PositionSeconds;
        m_state.DurationSeconds = timeline.DurationSeconds;
        if (update.Volume && m_audioSessionSerial == 0)
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
        if(update.Buffering)
        {
            m_pausedForCache=*update.Buffering;
        }
        m_state.Buffering = ResolveBufferingState(
            m_state.Buffering,
            update.Buffering,
            update.PlaybackReady.value_or(false),
            m_pausedForCache);
        if (update.PlaybackReady.value_or(false))
        {
            m_state.FirstFrameReady = true;
        }
        if (update.Ended && *update.Ended)
        {
            m_state.Paused = true;
        }
        if (update.Video)
        {
            m_state.Video = *update.Video;
        }
        if (update.Tracks)
        {
            m_state.Tracks = std::move(*update.Tracks);
            m_state.TracksReady = true;
        }
        if(update.FileLoaded&&*update.FileLoaded){++m_state.FileSerial;m_state.EndReason=PlaybackEndReason::None;m_audioSessionSerial=0;}
        if(update.EndReason){m_state.EndReason=*update.EndReason;++m_state.EndSerial;m_state.SeekPending=false;if(*update.EndReason==PlaybackEndReason::Error)m_state.Buffering=false;}
        SynchronizeAudioSession();
        NotifyChanged();
    }

    void MpvEngine::Replay()
    {
        if (m_source.Location.empty())
        {
            return;
        }
        m_seekTarget.reset();
        m_seekRestarted = false;
        m_state.PositionSeconds = 0.0;
        m_state.DurationSeconds = 0.0;
        m_state.Buffering = true;
        m_state.SeekPending = false;
        m_state.FirstFrameReady = false;
        m_pausedForCache = false;
        m_state.TracksReady = false;
        m_state.EndReason = PlaybackEndReason::None;
        m_state.Tracks.clear();
        // The format survives on purpose: a replay reopens the same file, so
        // clearing it would blink the quality badge off and straight back on.
        if (m_client)
        {
            m_client->Open(m_source);
        }
        NotifyChanged();
    }

    void MpvEngine::SynchronizeAudioSession()
    {
        if (m_state.FileSerial == 0 || m_audioSessionSerial == m_state.FileSerial)
        {
            return;
        }
        auto const session = WindowsAudioSession::Read();
        if (!session)
        {
            return;
        }
        m_audioSessionSerial = m_state.FileSerial;
        if (m_client)
        {
            m_client->SetVolume(100.0);
        }
        m_state.Volume = session->Muted ? 0.0 : session->Volume * 100.0;
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
