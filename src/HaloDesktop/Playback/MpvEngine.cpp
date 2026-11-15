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
        if (!m_surfaceAttached)
        {
            throw std::logic_error("A video surface must be attached before playback starts");
        }

        auto const dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        if (!dispatcher)
        {
            throw winrt::hresult_wrong_thread();
        }

        auto const weak = weak_from_this();
        m_client = std::make_unique<MpvClient>(m_surfaceSize, dispatcher, [weak](PlaybackUpdate update) {
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
            m_client->SetVideoFit(m_state.VideoFit);
            if (m_state.Paused)
            {
                m_client->SetPaused(true);
            }
            if (!m_source.Location.empty())
            {
                m_loadGeneration = m_client->Open(m_source, MpvClient::TrackChoice::Automatic);
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
        // The panel must release the swapchain before libmpv destroys it, so it
        // is never left presenting an object that no longer exists.
        PublishSwapChain(0);
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
        m_endHandled = false;
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
        // A header the source asked for that cannot be sent is dropped on its own,
        // so the stream still plays without it (the reference's `playbackHeaders`).
        source.Headers = Security::UsableProtectedHttpHeaders(source.Headers);

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
        m_endHandled = false;
        // Framing is the viewer's device preference and is kept across files,
        // as the reference keeps `fillPicture`: the player sets it on activation
        // and on every toggle, and nothing here resets it.
        if (m_client)
        {
            m_loadGeneration = m_client->Open(m_source, MpvClient::TrackChoice::Automatic);
        }
        NotifyChanged();
    }

    void MpvEngine::AttachVideoSurface(VideoSurfaceSize size, VideoSwapChainHandler handler)
    {
        if (!handler)
        {
            throw std::invalid_argument("A swapchain handler is required");
        }

        auto const restart = m_running;
        if (restart)
        {
            Stop();
        }
        m_surfaceSize = size;
        m_swapChainHandler = std::move(handler);
        m_surfaceAttached = true;
        if (restart)
        {
            Start();
        }
    }

    void MpvEngine::SetVideoSurfaceSize(VideoSurfaceSize size)
    {
        if (m_surfaceSize == size)
        {
            return;
        }
        // Committed once the request is queued. A request libmpv later refuses
        // clears the recorded size (ApplyFailedCommand), so the guard above cannot
        // swallow the next one and leave the swapchain at a stale size.
        if (m_client)
        {
            m_client->SetSurfaceSize(size);
        }
        m_surfaceSize = size;
    }

    void MpvEngine::DetachVideoSurface() noexcept
    {
        Stop();
        m_surfaceAttached = false;
        m_swapChainHandler = {};
    }

    void MpvEngine::PublishSwapChain(std::uintptr_t address) noexcept
    {
        if (m_publishedSwapChain == address)
        {
            return;
        }
        m_publishedSwapChain = address;
        if (!m_swapChainHandler)
        {
            return;
        }
        try
        {
            m_swapChainHandler(address);
        }
        catch (...)
        {
        }
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
            // Queued; a seek libmpv refuses comes back as a failed command, which
            // releases the target held below.
            m_seekGeneration = m_client->SeekAbsolute(next);
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
            m_seekGeneration = m_client->SeekRelative(seconds);
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

    void MpvEngine::SetVideoFit(VideoFitMode mode)
    {
        if (m_state.VideoFit == mode)
        {
            return;
        }
        m_state.VideoFit = mode;
        if (m_client)
        {
            m_client->SetVideoFit(mode);
        }
        NotifyChanged();
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

    void MpvEngine::ApplyFailedCommand(MpvCommandFailure const& failure)
    {
        switch (failure.Kind)
        {
        case MpvCommandKind::Load:
            // Only the latest load can fail the file on screen; an older one was
            // already replaced. Answered as libmpv answers a file that cannot
            // play, so the page shows the same prompt.
            if (failure.Generation != m_loadGeneration)
            {
                return;
            }
            m_state.EndReason = PlaybackEndReason::Error;
            ++m_state.EndSerial;
            m_state.Paused = true;
            m_state.Buffering = false;
            m_state.SeekPending = false;
            m_seekTarget.reset();
            m_seekRestarted = false;
            break;
        case MpvCommandKind::Seek:
            // The playhead stops waiting for a target libmpv never took.
            if (failure.Generation != m_seekGeneration)
            {
                return;
            }
            m_seekTarget.reset();
            m_seekRestarted = false;
            break;
        case MpvCommandKind::Subtitle:
            ++m_state.SubtitleCommandFailureSerial;
            break;
        case MpvCommandKind::SurfaceSize:
            // Forgotten, so the panel's next size is sent again rather than being
            // taken as already applied.
            m_surfaceSize = { 0, 0 };
            return;
        case MpvCommandKind::Other:
        default:
            return;
        }
        NotifyChanged();
    }

    void MpvEngine::ApplyUpdate(PlaybackUpdate update)
    {
        if (update.FailedCommand)
        {
            ApplyFailedCommand(*update.FailedCommand);
            return;
        }
        if (update.SwapChainAddress)
        {
            PublishSwapChain(*update.SwapChainAddress);
        }
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
        if (update.PlaybackReady.value_or(false))
        {
            // Playback restart is the decoder's authoritative recovery edge.
            // Clear the cached pause marker before resolving the visible state.
            m_pausedForCache = false;
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
        if (update.Video)
        {
            m_state.Video = *update.Video;
        }
        if (update.Tracks)
        {
            m_state.Tracks = std::move(*update.Tracks);
            m_state.TracksReady = true;
        }
        if(update.FileLoaded&&*update.FileLoaded)
        {
            ++m_state.FileSerial;m_state.EndReason=PlaybackEndReason::None;m_audioSessionSerial=0;
            static_cast<void>(AcceptEndOfFileSignal(m_endHandled,EndOfFileSignal::FileLoaded));
        }
        // Under keep-open the end of a file is eof-reached turning true, handled
        // once; a seek back clears it and re-arms the end. An end-file eof (no
        // keep-open) is the same end and is not handled twice.
        std::optional<PlaybackEndReason> endReason;
        if(update.EofReached)
        {
            if(*update.EofReached)
            {
                if(AcceptEndOfFileSignal(m_endHandled,EndOfFileSignal::EofReached))endReason=PlaybackEndReason::Eof;
            }
            else
            {
                static_cast<void>(AcceptEndOfFileSignal(m_endHandled,EndOfFileSignal::EofCleared));
                if(m_state.EndReason==PlaybackEndReason::Eof)m_state.EndReason=PlaybackEndReason::None;
            }
        }
        if(update.EndReason)
        {
            if(*update.EndReason!=PlaybackEndReason::Eof||AcceptEndOfFileSignal(m_endHandled,EndOfFileSignal::EndFileEof))
            {
                endReason=*update.EndReason;
            }
        }
        if(endReason)
        {
            m_state.EndReason=*endReason;
            ++m_state.EndSerial;
            m_state.Paused=true;
            m_state.Buffering=false;
            m_state.SeekPending=false;
        }
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
        m_endHandled = false;
        // The format survives on purpose: a replay reopens the same file, so
        // clearing it would blink the quality badge off and straight back on.
        if (m_client)
        {
            m_loadGeneration = m_client->Open(m_source, MpvClient::TrackChoice::Keep);
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
