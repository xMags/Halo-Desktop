#pragma once

#include "Playback/IPlaybackEngine.h"
#include "Playback/MpvCallQueue.h"

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
    // What a queued mpv call was for, so its failure can be answered in kind.
    enum class MpvCommandKind
    {
        Load,
        Seek,
        Subtitle,
        SurfaceSize,
        Other,
    };

    // A queued call that mpv refused. Generation is the number the call's method
    // returned when it was queued, so a late failure can be told from the latest.
    struct MpvCommandFailure final
    {
        MpvCommandKind Kind{ MpvCommandKind::Other };
        std::uint64_t Generation{};
    };

    struct PlaybackUpdate final
    {
        std::optional<double> PositionSeconds;
        std::optional<double> DurationSeconds;
        std::optional<double> Volume;
        std::optional<double> Speed;
        std::optional<bool> Paused;
        std::optional<bool> Buffering;
        std::optional<bool> FileLoaded;
        std::optional<bool> PlaybackReady;
        std::optional<PlaybackEndReason> EndReason;
        // mpv's eof-reached under keep-open: true once the file has played out
        // and holds its last frame, false again after a seek back from there.
        std::optional<bool> EofReached;
        // True while libmpv is between a seek request and the restart that completes it.
        std::optional<bool> Seeking;
        std::optional<VideoFormat> Video;
        std::optional<std::vector<TrackInfo>> Tracks;
        // Address of the D3D11 swapchain, 0 once libmpv has released it.
        std::optional<std::uintptr_t> SwapChainAddress;
        std::optional<MpvCommandFailure> FailedCommand;
    };

    // Owns one libmpv handle, its event thread and its command worker.
    //
    // Threads: methods are called on the UI thread. Every command and property
    // set is queued, in call order, to one worker (MpvCallQueue) that alone calls
    // into libmpv for them, so a slow call (a subtitle file being parsed, a load)
    // never holds the window. The event thread translates libmpv events. Both hand
    // results back to the UI thread through the dispatcher, and only while the
    // client is alive. No lock is held while calling libmpv or a handler.
    //
    // Failures: a queued call that libmpv refuses arrives later, on the UI thread,
    // as an update carrying FailedCommand. Input that cannot be handed to libmpv at
    // all (text that is not valid Unicode) still throws from the method itself.
    class MpvClient final
    {
    public:
        using UpdateHandler = std::function<void(PlaybackUpdate)>;

        MpvClient(VideoSurfaceSize surfaceSize,
                  winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher,
                  UpdateHandler updateHandler,
                  bool hardwareDecoding);
        ~MpvClient();

        MpvClient(MpvClient const&) = delete;
        MpvClient& operator=(MpvClient const&) = delete;
        MpvClient(MpvClient&&) = delete;
        MpvClient& operator=(MpvClient&&) = delete;

        // Automatic resets the audio and subtitle choice for a new file; a replay of
        // the same file keeps what the viewer picked.
        enum class TrackChoice
        {
            Automatic,
            Keep,
        };
        // Load and seeks return the generation their failure would carry.
        std::uint64_t Open(PlaybackSource const& source, TrackChoice trackChoice);
        void SetPaused(bool paused);
        std::uint64_t SeekAbsolute(double seconds);
        std::uint64_t SeekRelative(double seconds);
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
        void SetVideoFit(VideoFitMode mode);
        void SetSurfaceSize(VideoSurfaceSize size);
        [[nodiscard]] double DurationSeconds() const noexcept;
        // Stops the command worker (discarding calls not yet started and waiting
        // for the one running), then quits libmpv, joins the event thread and
        // destroys the handle. Nothing reaches the UI after it begins.
        void Shutdown() noexcept;

    private:
        static mpv_handle* CreateInitializedHandle(
            VideoSurfaceSize surfaceSize,
            char const* videoOutput,
            bool hardwareDecoding,
            int& initializationError);
        void EventLoop() noexcept;
        void DispatchUpdate(PlaybackUpdate update) noexcept;
        // Queues one call for the worker and returns its generation.
        std::uint64_t Submit(MpvCommandKind kind, std::function<void(mpv_handle*)> call);

        // The worker starts before the handle exists and is stopped by Shutdown
        // before it is destroyed; declared first so it is also destroyed last.
        MpvCallQueue m_calls;
        mpv_handle* m_handle{};
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        UpdateHandler m_updateHandler;
        std::shared_ptr<std::atomic_bool> m_dispatchAlive{ std::make_shared<std::atomic_bool>(true) };
        std::atomic_bool m_stopping{};
        // Read and written on the UI thread only.
        std::uint64_t m_nextGeneration{};
        std::jthread m_eventThread;
    };
} // namespace HaloDesktop::Playback
