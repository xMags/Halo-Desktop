#include "pch.h"
#include "Playback/MpvScrubPreviewSource.h"
#include "Playback/MpvCommand.h"
#include "Playback/ScrubPreviewPolicy.h"

#include <mpv/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    using namespace std::chrono_literals;

    // The preview never plays, so its event pump only has to be responsive enough to
    // notice a superseding scrub or a shutdown. Polling this often costs nothing while
    // idle, because the pump runs only during a decode.
    constexpr double EventPollSeconds = 0.05;
    constexpr auto FileLoadTimeout = 20s;
    constexpr auto SeekTimeout = 5s;

    // Frames are scaled down inside mpv, so an accepted frame is normally this narrow.
    // The bounds below are the safety net for a build where the filter is unavailable:
    // an oversized frame is averaged down here rather than dropped, so previews degrade
    // in cost rather than disappearing.
    constexpr std::int32_t PreviewTargetWidth = 320;
    constexpr std::int32_t MaximumSourceWidth = 4096;
    constexpr std::int32_t MaximumSourceHeight = 2304;

    struct NodeContents final
    {
        mpv_node Value{};

        NodeContents() = default;
        ~NodeContents() { mpv_free_node_contents(&Value); }
        NodeContents(NodeContents const&) = delete;
        NodeContents& operator=(NodeContents const&) = delete;
        NodeContents(NodeContents&&) = delete;
        NodeContents& operator=(NodeContents&&) = delete;
    };

    mpv_node const* FindMapValue(mpv_node const& map, std::string_view key) noexcept
    {
        if (map.format != MPV_FORMAT_NODE_MAP || !map.u.list)
        {
            return nullptr;
        }

        auto const list = map.u.list;
        for (int index = 0; index < list->num; ++index)
        {
            if (list->keys[index] && key == list->keys[index])
            {
                return &list->values[index];
            }
        }
        return nullptr;
    }

    std::int64_t ReadInteger(mpv_node const& map, std::string_view key) noexcept
    {
        auto const value = FindMapValue(map, key);
        if (!value || value->format != MPV_FORMAT_INT64)
        {
            return 0;
        }
        return value->u.int64;
    }

    std::string_view ReadString(mpv_node const& map, std::string_view key) noexcept
    {
        auto const value = FindMapValue(map, key);
        if (!value || value->format != MPV_FORMAT_STRING || !value->u.string)
        {
            return {};
        }
        return value->u.string;
    }

    // mpv hands back either bgr0, whose fourth byte carries no alpha at all, or bgra.
    // Anything else is refused rather than reinterpreted: a wrong guess would paint
    // colour-swapped noise over the seek bar.
    bool IsSupportedFormat(std::string_view format) noexcept
    {
        return format == "bgr0" || format == "bgra";
    }

    // Averages whole source blocks into each destination pixel. The factor is an integer
    // because the only caller needing it is the unscaled fallback, where the source is a
    // clean multiple of the target often enough for this to look right and stay cheap.
    void CopyScaled(
        std::uint8_t const* source,
        std::int32_t sourceWidth,
        std::int32_t sourceHeight,
        std::int64_t stride,
        std::int32_t factor,
        HaloDesktop::Playback::ScrubPreviewFrame& frame)
    {
        frame.Width = sourceWidth / factor;
        frame.Height = sourceHeight / factor;
        if (frame.Width <= 0 || frame.Height <= 0)
        {
            return;
        }

        frame.Bgra.resize(
            static_cast<std::size_t>(frame.Width) * static_cast<std::size_t>(frame.Height) * 4u);
        auto const samples = static_cast<std::uint32_t>(factor) * static_cast<std::uint32_t>(factor);
        for (std::int32_t y = 0; y < frame.Height; ++y)
        {
            for (std::int32_t x = 0; x < frame.Width; ++x)
            {
                std::uint32_t blue{};
                std::uint32_t green{};
                std::uint32_t red{};
                for (std::int32_t offsetY = 0; offsetY < factor; ++offsetY)
                {
                    auto const* row = source + (static_cast<std::int64_t>(y) * factor + offsetY) * stride;
                    for (std::int32_t offsetX = 0; offsetX < factor; ++offsetX)
                    {
                        auto const* pixel = row + (static_cast<std::int64_t>(x) * factor + offsetX) * 4;
                        blue += pixel[0];
                        green += pixel[1];
                        red += pixel[2];
                    }
                }

                auto* destination = frame.Bgra.data()
                    + (static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.Width)
                       + static_cast<std::size_t>(x)) * 4u;
                destination[0] = static_cast<std::uint8_t>(blue / samples);
                destination[1] = static_cast<std::uint8_t>(green / samples);
                destination[2] = static_cast<std::uint8_t>(red / samples);
                // bgr0 leaves the fourth byte undefined, and a bitmap drawn with a zero
                // alpha is invisible, so opacity is asserted rather than copied.
                destination[3] = 255;
            }
        }
    }

    mpv_handle* CreateInitializedHandle() noexcept
    {
        auto* handle = mpv_create();
        if (!handle)
        {
            return nullptr;
        }

        try
        {
            for (auto const& option : HaloDesktop::Playback::ScrubPreviewMpvOptions())
            {
                HaloDesktop::Playback::CheckMpvResult(
                    option.Name,
                    mpv_set_option_string(handle, option.Name, option.Value));
            }

            // A failure here is not fatal. Without the filter every grab costs a
            // full-resolution frame, which the copy below then averages down instead.
            for (auto const* filter : HaloDesktop::Playback::ScrubPreviewScaleFilters())
            {
                if (mpv_set_option_string(handle, "vf", filter) >= 0)
                {
                    break;
                }
            }

            if (mpv_initialize(handle) < 0)
            {
                mpv_terminate_destroy(handle);
                return nullptr;
            }
            return handle;
        }
        catch (...)
        {
            mpv_terminate_destroy(handle);
            return nullptr;
        }
    }

    void DestroyHandle(mpv_handle* handle) noexcept
    {
        if (!handle)
        {
            return;
        }

        char const* quitArguments[] = { "quit", nullptr };
        static_cast<void>(mpv_command(handle, quitArguments));
        // quit is asynchronous, and an open network demuxer can otherwise keep the
        // teardown below waiting long enough to stall closing the player.
        mpv_wakeup(handle);
        mpv_terminate_destroy(handle);
    }
}

namespace HaloDesktop::Playback
{
    MpvScrubPreviewSource::~MpvScrubPreviewSource()
    {
        Close();
    }

    void MpvScrubPreviewSource::Open(PlaybackSource source)
    {
        Close();

        auto const dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        std::lock_guard const guard{ m_mutex };
        m_source = std::move(source);
        m_dispatcher = dispatcher;
        m_dispatchAlive = std::make_shared<std::atomic_bool>(true);
        m_pending.reset();
        m_nextRequestId = 0;
        m_currentRequestId = 0;
        m_lastIssuedSeconds = 0.0;
        m_hasIssued = false;
        m_stopping = false;
        m_disabled = false;
        m_workerStarted = false;
    }

    void MpvScrubPreviewSource::Close() noexcept
    {
        {
            std::lock_guard const guard{ m_mutex };
            m_stopping = true;
            m_pending.reset();
            if (m_dispatchAlive)
            {
                m_dispatchAlive->store(false);
            }
        }
        m_wake.notify_all();
        if (m_worker.joinable())
        {
            m_worker.join();
        }

        std::lock_guard const guard{ m_mutex };
        m_source = {};
        m_workerStarted = false;
    }

    std::uint64_t MpvScrubPreviewSource::Request(double seconds)
    {
        bool startWorker{};
        std::uint64_t requestId{};
        {
            std::lock_guard const guard{ m_mutex };
            if (m_stopping || m_disabled || m_source.Location.empty())
            {
                return m_currentRequestId;
            }
            if (!ShouldIssueScrubPreview(seconds, m_lastIssuedSeconds, m_hasIssued))
            {
                return m_currentRequestId;
            }

            requestId = ++m_nextRequestId;
            m_currentRequestId = requestId;
            m_lastIssuedSeconds = seconds;
            m_hasIssued = true;
            m_pending = PendingRequest{ .Seconds = seconds, .Id = requestId };
            startWorker = !m_workerStarted;
            m_workerStarted = true;
        }

        if (startWorker)
        {
            // Started here rather than in Open so a session where nobody scrubs never
            // opens a second connection to the origin.
            m_worker = std::jthread([this] { WorkerLoop(); });
        }
        m_wake.notify_all();
        return requestId;
    }

    void MpvScrubPreviewSource::SetFrameHandler(ScrubPreviewFrameHandler handler)
    {
        std::lock_guard const guard{ m_mutex };
        m_frameHandler = std::move(handler);
    }

    void MpvScrubPreviewSource::ClearFrameHandler() noexcept
    {
        std::lock_guard const guard{ m_mutex };
        m_frameHandler = nullptr;
    }

    void MpvScrubPreviewSource::Disable() noexcept
    {
        std::lock_guard const guard{ m_mutex };
        m_disabled = true;
        m_pending.reset();
    }

    bool MpvScrubPreviewSource::Stopping() const noexcept
    {
        std::lock_guard const guard{ m_mutex };
        return m_stopping;
    }

    bool MpvScrubPreviewSource::Superseded(std::uint64_t requestId) const noexcept
    {
        std::lock_guard const guard{ m_mutex };
        return m_stopping || (m_pending && m_pending->Id != requestId);
    }

    std::optional<MpvScrubPreviewSource::PendingRequest> MpvScrubPreviewSource::WaitForRequest()
    {
        std::unique_lock lock{ m_mutex };
        m_wake.wait(lock, [this] { return m_stopping || m_pending.has_value(); });
        if (m_stopping)
        {
            return std::nullopt;
        }
        auto request = m_pending;
        m_pending.reset();
        return request;
    }

    bool MpvScrubPreviewSource::AwaitFirstFrame(mpv_handle* handle) noexcept
    {
        auto const deadline = std::chrono::steady_clock::now() + FileLoadTimeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (Stopping())
            {
                return false;
            }

            auto const* event = mpv_wait_event(handle, EventPollSeconds);
            if (!event || event->event_id == MPV_EVENT_SHUTDOWN)
            {
                return false;
            }
            // Deliberately the restart rather than the file-loaded event. Opening a file
            // emits its own restart once the first frame is ready, and leaving that one
            // queued would let the first seek mistake it for its own completion and grab
            // the frame it was seeking away from.
            if (event->event_id == MPV_EVENT_PLAYBACK_RESTART)
            {
                return true;
            }
            if (event->event_id == MPV_EVENT_END_FILE)
            {
                auto const* endFile = static_cast<mpv_event_end_file const*>(event->data);
                if (endFile && endFile->reason == MPV_END_FILE_REASON_ERROR)
                {
                    return false;
                }
            }
        }
        return false;
    }

    bool MpvScrubPreviewSource::AwaitSeekCompletion(mpv_handle* handle, std::uint64_t requestId) noexcept
    {
        auto const deadline = std::chrono::steady_clock::now() + SeekTimeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (Superseded(requestId))
            {
                return false;
            }

            auto const* event = mpv_wait_event(handle, EventPollSeconds);
            if (!event || event->event_id == MPV_EVENT_SHUTDOWN)
            {
                return false;
            }
            if (event->event_id == MPV_EVENT_PLAYBACK_RESTART)
            {
                return true;
            }
        }
        return false;
    }

    void MpvScrubPreviewSource::DecodeAndDeliver(mpv_handle* handle, PendingRequest const& request) noexcept
    {
        // Anything still queued belongs to an earlier request, and a leftover restart
        // would end this one's wait before its own frame exists.
        while (auto const* pending = mpv_wait_event(handle, 0.0))
        {
            if (pending->event_id == MPV_EVENT_NONE)
            {
                break;
            }
            if (pending->event_id == MPV_EVENT_SHUTDOWN)
            {
                return;
            }
        }

        try
        {
            // Keyframe seeks, because a preview does not need frame accuracy and an
            // exact seek would decode a whole group of pictures for one thumbnail.
            RunMpvCommand(handle, { "seek", std::to_string(request.Seconds), "absolute+keyframes" });
        }
        catch (...)
        {
            return;
        }

        if (!AwaitSeekCompletion(handle, request.Id))
        {
            return;
        }

        NodeContents result;
        char const* arguments[] = { "screenshot-raw", "video", nullptr };
        if (mpv_command_ret(handle, arguments, &result.Value) < 0)
        {
            return;
        }

        auto const width = ReadInteger(result.Value, "w");
        auto const height = ReadInteger(result.Value, "h");
        auto const stride = ReadInteger(result.Value, "stride");
        if (width <= 0 || height <= 0 || width > MaximumSourceWidth || height > MaximumSourceHeight
            || stride < width * 4)
        {
            return;
        }
        if (!IsSupportedFormat(ReadString(result.Value, "format")))
        {
            return;
        }

        auto const* data = FindMapValue(result.Value, "data");
        if (!data || data->format != MPV_FORMAT_BYTE_ARRAY || !data->u.ba || !data->u.ba->data)
        {
            return;
        }
        if (data->u.ba->size < static_cast<std::size_t>(stride) * static_cast<std::size_t>(height))
        {
            return;
        }

        try
        {
            auto const factor = std::max<std::int32_t>(
                1,
                static_cast<std::int32_t>(width) / PreviewTargetWidth);
            ScrubPreviewFrame frame{ .RequestId = request.Id, .Seconds = request.Seconds };
            CopyScaled(
                static_cast<std::uint8_t const*>(data->u.ba->data),
                static_cast<std::int32_t>(width),
                static_cast<std::int32_t>(height),
                stride,
                factor,
                frame);
            if (frame.Bgra.empty() || Superseded(request.Id))
            {
                return;
            }
            Deliver(std::move(frame));
        }
        catch (...)
        {
        }
    }

    void MpvScrubPreviewSource::Deliver(ScrubPreviewFrame frame) noexcept
    {
        try
        {
            std::shared_ptr<std::atomic_bool> alive;
            ScrubPreviewFrameHandler handler;
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{ nullptr };
            {
                std::lock_guard const guard{ m_mutex };
                alive = m_dispatchAlive;
                handler = m_frameHandler;
                dispatcher = m_dispatcher;
            }
            if (!alive || !handler || !dispatcher)
            {
                return;
            }

            static_cast<void>(dispatcher.TryEnqueue(
                [alive, handler, frame = std::move(frame)]() mutable {
                    if (alive->load())
                    {
                        try
                        {
                            handler(std::move(frame));
                        }
                        catch (...)
                        {
                        }
                    }
                }));
        }
        catch (...)
        {
        }
    }

    void MpvScrubPreviewSource::WorkerLoop() noexcept
    {
        PlaybackSource source;
        {
            std::lock_guard const guard{ m_mutex };
            source = m_source;
        }

        auto* handle = CreateInitializedHandle();
        if (!handle)
        {
            Disable();
            return;
        }

        try
        {
            LoadMpvSource(handle, source);
        }
        catch (...)
        {
            Disable();
            DestroyHandle(handle);
            return;
        }

        if (!AwaitFirstFrame(handle))
        {
            // The origin refused, timed out, or the player is closing. Either way this
            // instance stops asking: a hover must never retry against a dead URL.
            Disable();
            DestroyHandle(handle);
            return;
        }

        while (auto const request = WaitForRequest())
        {
            DecodeAndDeliver(handle, *request);
        }

        DestroyHandle(handle);
    }
}
