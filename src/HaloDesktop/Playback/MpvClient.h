#pragma once

#include "Playback/IPlaybackEngine.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <winrt/Microsoft.UI.Dispatching.h>

struct mpv_handle;

namespace HaloDesktop::Playback
{
    struct PlaybackUpdate final
    {
        std::optional<double> PositionSeconds;
        std::optional<double> DurationSeconds;
        std::optional<double> Volume;
        std::optional<double> Speed;
        std::optional<bool> Paused;
        std::optional<bool> Buffering;
        std::optional<bool> Ended;
        std::optional<bool> FileLoaded;
        std::optional<bool> PlaybackReady;
        std::optional<PlaybackEndReason> EndReason;
        // True while libmpv is between a seek request and the restart that completes it.
        std::optional<bool> Seeking;
        std::optional<std::vector<TrackInfo>> Tracks;
    };

    // Owns one libmpv handle and its event thread. Commands are issued by the
    // UI thread; translated updates are dispatched back to that same thread.
    class MpvClient final
    {
    public:
        using UpdateHandler = std::function<void(PlaybackUpdate)>;

        MpvClient(std::uintptr_t videoWindowHandle,
                  winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher, UpdateHandler updateHandler);
        ~MpvClient();

        MpvClient(MpvClient const&) = delete;
        MpvClient& operator=(MpvClient const&) = delete;
        MpvClient(MpvClient&&) = delete;
        MpvClient& operator=(MpvClient&&) = delete;

        void Open(PlaybackSource const& source);
        void Replay();
        void SetPaused(bool paused);
        void SeekAbsolute(double seconds);
        void SeekRelative(double seconds);
        void SetVolume(double volume);
        void SetSpeed(double speed);
        void SetAudioTrack(std::int64_t id);
        void SetSubtitleTrack(std::optional<std::int64_t> id);
        void SetSubtitleDelay(double seconds);
        void SetAudioDelay(double seconds);
        void AddExternalSubtitle(
            std::wstring const& path,
            std::wstring const& identity,
            std::wstring const& displayTitle,
            std::wstring const& language);
        void RemoveTrack(std::int64_t id);
        void ApplySubtitleStyle(SubtitleStyle const& style);
        [[nodiscard]] double DurationSeconds() const noexcept;
        void Shutdown() noexcept;

    private:
        static mpv_handle* CreateInitializedHandle(std::uintptr_t videoWindowHandle, char const* videoOutput,
                                                   int& initializationError);
        void EventLoop() noexcept;
        void DispatchUpdate(PlaybackUpdate update) noexcept;
        void Command(std::vector<std::string> const& arguments);
        void SetDoubleProperty(char const* name, double value);
        void SetInt64Property(char const* name, std::int64_t value);
        void SetStringProperty(char const* name,std::wstring const& value);

        mpv_handle* m_handle{};
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        UpdateHandler m_updateHandler;
        std::shared_ptr<std::atomic_bool> m_dispatchAlive{ std::make_shared<std::atomic_bool>(true) };
        std::atomic_bool m_stopping{};
        std::jthread m_eventThread;
    };
} // namespace HaloDesktop::Playback
