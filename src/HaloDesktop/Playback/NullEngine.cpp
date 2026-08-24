#include "pch.h"
#include "Playback/NullEngine.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace HaloDesktop::Playback
{
    NullEngine::NullEngine()
    {
        m_state.PositionSeconds = 29.0 * 60.0 + 12.0;
        m_state.DurationSeconds = 47.0 * 60.0 + 36.0;
        m_state.Tracks = {
            { 1, TrackType::Audio, L"English 5.1", L"Track 1 · 48 kHz · default", L"EAC3", true },
            { 2, TrackType::Audio, L"English 2.0", L"Track 2 · 48 kHz · commentary", L"AAC", false },
            { 3, TrackType::Audio, L"Japanese 5.1", L"Track 3 · 48 kHz", L"EAC3", false },
            { 1, TrackType::Subtitle, L"English", L"Track 1 · full", L"ASS", true },
            { 2, TrackType::Subtitle, L"English SDH", L"Track 2 · hearing impaired", L"SRT", false },
            { 3, TrackType::Subtitle, L"Japanese", L"Track 3 · signs & songs", L"ASS", false },
        };
    }
    NullEngine::~NullEngine()
    {
        Stop();
    }
    void NullEngine::Start()
    {
        if (m_running)
        {
            return;
        }
        auto const dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        if (!dispatcher)
        {
            throw winrt::hresult_wrong_thread();
        }
        m_timer = dispatcher.CreateTimer();
        m_timer.Interval(std::chrono::milliseconds(250));
        m_timer.IsRepeating(true);
        m_tickRevoker =
            m_timer.Tick(winrt::auto_revoke,
                         [this]([[maybe_unused]] winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer const& timer,
                                [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args) { Tick(); });
        m_lastTick = std::chrono::steady_clock::now();
        m_timer.Start();
        m_running = true;
        NotifyChanged();
    }
    void NullEngine::Stop() noexcept
    {
        try
        {
            if (m_timer)
            {
                m_timer.Stop();
            }
        }
        catch (...)
        {
        }
        m_tickRevoker.revoke();
        m_timer = nullptr;
        m_running = false;
    }
    void NullEngine::Open(std::wstring const& source)
    {
        m_source = source;
        ++m_state.FileSerial;
        m_state.EndReason=PlaybackEndReason::None;
        NotifyChanged();
    }
    void NullEngine::AttachVideoWindow(std::uintptr_t windowHandle)
    {
        m_windowHandle = windowHandle;
    }
    void NullEngine::DetachVideoWindow() noexcept
    {
        m_windowHandle = 0;
    }
    void NullEngine::SetPaused(bool paused)
    {
        if (m_state.Paused != paused)
        {
            m_state.Paused = paused;
            m_lastTick = std::chrono::steady_clock::now();
            NotifyChanged();
        }
    }
    void NullEngine::SeekAbsolute(double seconds)
    {
        auto const next = std::clamp(seconds, 0.0, m_state.DurationSeconds);
        if (std::abs(next - m_state.PositionSeconds) < 0.001)
        {
            return;
        }
        m_state.PositionSeconds = next;
        NotifyChanged();
    }
    void NullEngine::SeekRelative(double seconds)
    {
        SeekAbsolute(m_state.PositionSeconds + seconds);
    }
    void NullEngine::SetVolume(double volume)
    {
        auto const next = std::clamp(volume, 0.0, 100.0);
        if (std::abs(next - m_state.Volume) < 0.001)
        {
            return;
        }
        m_state.Volume = next;
        NotifyChanged();
    }
    void NullEngine::SetSpeed(double speed)
    {
        auto const next = std::clamp(speed, 0.5, 2.0);
        if (std::abs(next - m_state.Speed) < 0.001)
        {
            return;
        }
        m_state.Speed = next;
        NotifyChanged();
    }
    void NullEngine::SetAudioTrack(std::int64_t id)
    {
        m_audioTrack = id;
        for (auto& track : m_state.Tracks)
        {
            if (track.Type == TrackType::Audio)
            {
                track.Selected = track.Id == id;
            }
        }
        NotifyChanged();
    }
    void NullEngine::SetSubtitleTrack(std::optional<std::int64_t> id)
    {
        m_subtitleTrack = id;
        for (auto& track : m_state.Tracks)
        {
            if (track.Type == TrackType::Subtitle)
            {
                track.Selected = id && track.Id == *id;
            }
        }
        NotifyChanged();
    }
    void NullEngine::SetSubtitleDelay(double seconds)
    {
        m_subtitleDelay = seconds;
        NotifyChanged();
    }
    void NullEngine::SetAudioDelay(double seconds)
    {
        m_audioDelay = seconds;
        NotifyChanged();
    }
    PlaybackState NullEngine::State() const
    {
        return m_state;
    }
    double NullEngine::DurationNow()const noexcept{return m_state.DurationSeconds;}
    PlaybackChangedToken NullEngine::AddChangedHandler(PlaybackChangedHandler handler)
    {
        if (!handler)
        {
            return 0;
        }
        auto const token = ++m_nextToken;
        m_handlers.emplace(token, std::move(handler));
        return token;
    }
    void NullEngine::RemoveChangedHandler(PlaybackChangedToken token) noexcept
    {
        m_handlers.erase(token);
    }
    void NullEngine::Tick()
    {
        auto const now = std::chrono::steady_clock::now();
        auto const elapsed = std::chrono::duration<double>(now - m_lastTick).count();
        m_lastTick = now;
        if (m_state.Paused || m_state.Buffering)
        {
            return;
        }
        m_state.PositionSeconds =
            (std::min)(m_state.DurationSeconds, m_state.PositionSeconds + elapsed * m_state.Speed);
        if (m_state.PositionSeconds >= m_state.DurationSeconds)
        {
            m_state.Paused = true;
        }
        NotifyChanged();
    }
    void NullEngine::NotifyChanged()
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
            handler();
        }
    }
} // namespace HaloDesktop::Playback
