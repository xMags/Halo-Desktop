#include "pch.h"
#include "Playback/MpvClient.h"
#include "Playback/MpvCommand.h"
#include "Playback/PlaybackPolicy.h"

#include <mpv/client.h>

#include <algorithm>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
    [[noreturn]] void ThrowMpvError(char const* operation, int error)
    {
        auto const description = mpv_error_string(error);
        throw std::runtime_error(std::string(operation) + ": " + (description ? description : "unknown libmpv error"));
    }

    void CheckMpv(char const* operation, int result)
    {
        if (result < 0)
        {
            ThrowMpvError(operation, result);
        }
    }

    std::string FormatSurfaceSize(HaloDesktop::Playback::VideoSurfaceSize size)
    {
        return std::to_string((std::max)(size.WidthPixels, 1u)) + "x"
            + std::to_string((std::max)(size.HeightPixels, 1u));
    }

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

    std::wstring ReadString(mpv_node const& map, std::string_view key)
    {
        auto const value = FindMapValue(map, key);
        if (!value || value->format != MPV_FORMAT_STRING || !value->u.string)
        {
            return {};
        }
        return std::wstring(winrt::to_hstring(value->u.string));
    }

    std::optional<std::int64_t> ReadInteger(mpv_node const& map, std::string_view key) noexcept
    {
        auto const value = FindMapValue(map, key);
        if (!value)
        {
            return std::nullopt;
        }
        if (value->format == MPV_FORMAT_INT64)
        {
            return value->u.int64;
        }
        if (value->format == MPV_FORMAT_DOUBLE)
        {
            return static_cast<std::int64_t>(value->u.double_);
        }
        return std::nullopt;
    }

    bool ReadFlag(mpv_node const& map, std::string_view key) noexcept
    {
        auto const value = FindMapValue(map, key);
        return value && value->format == MPV_FORMAT_FLAG && value->u.flag != 0;
    }

    std::int32_t ClampVideoDimension(std::optional<std::int64_t> value) noexcept
    {
        if (!value || *value <= 0)
        {
            return 0;
        }
        return static_cast<std::int32_t>(
            (std::min)(*value, static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::max)())));
    }

    HaloDesktop::Playback::VideoFormat ReadVideoFormat(mpv_node const& node)
    {
        HaloDesktop::Playback::VideoFormat format;
        if (node.format != MPV_FORMAT_NODE_MAP)
        {
            return format;
        }

        // dw and dh are the display size after aspect correction; w and h are what
        // the decoder produced, which is narrower for anamorphic content. Fall back
        // to the decoded size only when the corrected size is absent.
        format.Width = ClampVideoDimension(ReadInteger(node, "dw"));
        format.Height = ClampVideoDimension(ReadInteger(node, "dh"));
        if (format.Width == 0 || format.Height == 0)
        {
            format.Width = ClampVideoDimension(ReadInteger(node, "w"));
            format.Height = ClampVideoDimension(ReadInteger(node, "h"));
        }

        // Dolby Vision outranks the transfer function: its base layer is usually
        // PQ, so testing gamma first would report every DV file as plain HDR.
        if (ReadInteger(node, "dolby-vision-profile"))
        {
            format.DynamicRange = HaloDesktop::Playback::VideoDynamicRange::DolbyVision;
            return format;
        }
        auto const gamma = ReadString(node, "gamma");
        if (gamma == L"pq")
        {
            format.DynamicRange = HaloDesktop::Playback::VideoDynamicRange::Hdr;
        }
        else if (gamma == L"hlg")
        {
            format.DynamicRange = HaloDesktop::Playback::VideoDynamicRange::Hlg;
        }
        return format;
    }

    std::wstring Uppercase(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](wchar_t character) { return static_cast<wchar_t>(std::towupper(character)); });
        return value;
    }

    std::vector<HaloDesktop::Playback::TrackInfo> ReadTracks(mpv_node const& node)
    {
        std::vector<HaloDesktop::Playback::TrackInfo> tracks;
        if (node.format != MPV_FORMAT_NODE_ARRAY || !node.u.list)
        {
            return tracks;
        }

        auto const list = node.u.list;
        tracks.reserve(static_cast<std::size_t>((std::max)(list->num, 0)));
        for (int index = 0; index < list->num; ++index)
        {
            auto const& entry = list->values[index];
            auto const id = ReadInteger(entry, "id");
            auto const type = ReadString(entry, "type");
            if (!id || (type != L"audio" && type != L"sub"))
            {
                continue;
            }

            auto const trackType =
                type == L"audio" ? HaloDesktop::Playback::TrackType::Audio : HaloDesktop::Playback::TrackType::Subtitle;
            auto title = ReadString(entry, "title");
            auto const language = ReadString(entry, "lang");
            auto const external=ReadFlag(entry,"external");std::wstring identity;
            if(external)
            {
                if(auto const decoded=HaloDesktop::Playback::DecodeExternalSubtitleTrackTitle(title))
                {
                    identity=decoded->first;title=decoded->second;
                }
            }
            title = HaloDesktop::Playback::TrackDisplayTitle(title, language, *id);

            auto note = L"Track " + std::to_wstring(*id);
            if (!language.empty() && language != title)
            {
                note += L" · " + language;
            }
            tracks.push_back({
                *id,
                trackType,
                std::move(title),
                std::move(note),
                Uppercase(ReadString(entry, "codec")),
                ReadFlag(entry, "selected"),
                external,
                language,
                std::move(identity),
            });
        }
        return tracks;
    }

    std::optional<HaloDesktop::Playback::PlaybackUpdate> TranslateProperty(mpv_event const& event)
    {
        auto const property = static_cast<mpv_event_property const*>(event.data);
        if (!property || !property->name)
        {
            return std::nullopt;
        }

        HaloDesktop::Playback::PlaybackUpdate update;
        auto const name = std::string_view(property->name);
        // The swapchain is read before the "no value" guard below: libmpv
        // reports a destroyed video output by making the property unavailable,
        // and the panel has to hear about that to let go of it.
        if (name == "display-swapchain")
        {
            auto const available = property->format == MPV_FORMAT_INT64 && property->data;
            auto const address = available ? *static_cast<std::int64_t const*>(property->data) : 0;
            update.SwapChainAddress = address > 0 ? static_cast<std::uintptr_t>(address) : 0;
            return update;
        }
        if (property->format == MPV_FORMAT_NONE || !property->data)
        {
            return std::nullopt;
        }

        if (name == "time-pos" && property->format == MPV_FORMAT_DOUBLE)
        {
            update.PositionSeconds = *static_cast<double const*>(property->data);
        }
        else if (name == "duration" && property->format == MPV_FORMAT_DOUBLE)
        {
            update.DurationSeconds = *static_cast<double const*>(property->data);
        }
        else if (name == "pause" && property->format == MPV_FORMAT_FLAG)
        {
            update.Paused = *static_cast<int const*>(property->data) != 0;
        }
        else if (name == "volume" && property->format == MPV_FORMAT_DOUBLE)
        {
            update.Volume = *static_cast<double const*>(property->data);
        }
        else if (name == "speed" && property->format == MPV_FORMAT_DOUBLE)
        {
            update.Speed = *static_cast<double const*>(property->data);
        }
        else if (name == "paused-for-cache" && property->format == MPV_FORMAT_FLAG)
        {
            update.Buffering = *static_cast<int const*>(property->data) != 0;
        }
        else if (name == "eof-reached" && property->format == MPV_FORMAT_FLAG)
        {
            update.EofReached = *static_cast<int const*>(property->data) != 0;
        }
        else if (name == "track-list" && property->format == MPV_FORMAT_NODE)
        {
            update.Tracks = ReadTracks(*static_cast<mpv_node const*>(property->data));
        }
        else if (name == "video-params" && property->format == MPV_FORMAT_NODE)
        {
            update.Video = ReadVideoFormat(*static_cast<mpv_node const*>(property->data));
        }
        else
        {
            return std::nullopt;
        }
        return update;
    }

    // MPV_EVENT_SEEK opens a window in which time-pos still reports the old
    // playhead; MPV_EVENT_PLAYBACK_RESTART closes it once the new position is live.
    std::optional<HaloDesktop::Playback::PlaybackUpdate> TranslateEvent(mpv_event const& event)
    {
        HaloDesktop::Playback::PlaybackUpdate update;
        switch (event.event_id)
        {
        case MPV_EVENT_PROPERTY_CHANGE:
            return TranslateProperty(event);
        case MPV_EVENT_SEEK:
            update.Seeking = true;
            return update;
        case MPV_EVENT_PLAYBACK_RESTART:
            update.Seeking = false;
            update.PlaybackReady = true;
            return update;
        case MPV_EVENT_FILE_LOADED:
            update.FileLoaded = true;
            return update;
        case MPV_EVENT_END_FILE:
        {
            auto const end = static_cast<mpv_event_end_file const*>(event.data);
            if (!end) return std::nullopt;
            if (end->reason == MPV_END_FILE_REASON_EOF) update.EndReason = HaloDesktop::Playback::PlaybackEndReason::Eof;
            else if (end->reason == MPV_END_FILE_REASON_ERROR) update.EndReason = HaloDesktop::Playback::PlaybackEndReason::Error;
            // Every other reason is this app's own doing: under keep-open a file
            // that played out is still loaded, so the next loadfile (Up Next, a
            // replay) stops it, and that stop lands after the new file was opened.
            // Taken as an end it would mark the new file paused and ended.
            else return std::nullopt;
            return update;
        }
        default:
            return std::nullopt;
        }
    }
} // namespace

namespace HaloDesktop::Playback
{
    MpvClient::MpvClient(VideoSurfaceSize surfaceSize,
                         winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher,
                         UpdateHandler updateHandler,
                         bool hardwareDecoding)
        : m_dispatcher(dispatcher), m_updateHandler(std::move(updateHandler))
    {
        if (!m_dispatcher || !m_updateHandler)
        {
            throw std::invalid_argument("MpvClient requires a dispatcher and update handler");
        }

        int initializationError{};
        m_handle = CreateInitializedHandle(surfaceSize, "gpu-next", hardwareDecoding, initializationError);
        if (!m_handle)
        {
            m_handle = CreateInitializedHandle(surfaceSize, "gpu", hardwareDecoding, initializationError);
        }
        if (!m_handle)
        {
            ThrowMpvError("mpv_initialize", initializationError);
        }

        m_eventThread = std::jthread([this] { EventLoop(); });
    }

    MpvClient::~MpvClient()
    {
        Shutdown();
    }

    mpv_handle* MpvClient::CreateInitializedHandle(
        VideoSurfaceSize surfaceSize,
        char const* videoOutput,
        bool hardwareDecoding,
        int& initializationError)
    {
        auto* handle = mpv_create();
        if (!handle)
        {
            throw std::runtime_error("mpv_create returned null");
        }

        try
        {
            CheckMpv("set vo", mpv_set_option_string(handle, "vo", videoOutput));
            // Composition mode presents into a swapchain instead of a window, so
            // there is no child HWND to compose above the XAML overlay.
            CheckMpv("set gpu-context", mpv_set_option_string(handle, "gpu-context", "d3d11"));
            CheckMpv("set d3d11-output-mode", mpv_set_option_string(handle, "d3d11-output-mode", "composition"));
            CheckMpv("set d3d11-composition-size",
                     mpv_set_option_string(handle, "d3d11-composition-size", FormatSurfaceSize(surfaceSize).c_str()));
            CheckMpv("set hwdec", mpv_set_option_string(
                handle,
                "hwdec",
                hardwareDecoding ? "auto-safe" : "no"));
            CheckMpv("set idle", mpv_set_option_string(handle, "idle", "yes"));
            // As the reference player: a file that plays out pauses on its last
            // frame and raises eof-reached instead of unloading, so the picture
            // stays up under Up Next and a seek back can play the end again.
            CheckMpv("set keep-open", mpv_set_option_string(handle, "keep-open", "yes"));
            CheckMpv("disable input bindings", mpv_set_option_string(handle, "input-default-bindings", "no"));
            CheckMpv("disable video keyboard", mpv_set_option_string(handle, "input-vo-keyboard", "no"));
            CheckMpv("disable cursor input", mpv_set_option_string(handle, "input-cursor", "no"));
            CheckMpv("disable cursor autohide", mpv_set_option_string(handle, "cursor-autohide", "no"));
            CheckMpv("disable OSC", mpv_set_option_string(handle, "osc", "no"));
            CheckMpv("disable ytdl", mpv_set_option_string(handle, "ytdl", "no"));

            initializationError = mpv_initialize(handle);
            if (initializationError < 0)
            {
                mpv_destroy(handle);
                return nullptr;
            }

            CheckMpv("observe time-pos", mpv_observe_property(handle, 0, "time-pos", MPV_FORMAT_DOUBLE));
            CheckMpv("observe duration", mpv_observe_property(handle, 0, "duration", MPV_FORMAT_DOUBLE));
            CheckMpv("observe pause", mpv_observe_property(handle, 0, "pause", MPV_FORMAT_FLAG));
            CheckMpv("observe volume", mpv_observe_property(handle, 0, "volume", MPV_FORMAT_DOUBLE));
            CheckMpv("observe speed", mpv_observe_property(handle, 0, "speed", MPV_FORMAT_DOUBLE));
            CheckMpv("observe track-list", mpv_observe_property(handle, 0, "track-list", MPV_FORMAT_NODE));
            CheckMpv("observe video-params", mpv_observe_property(handle, 0, "video-params", MPV_FORMAT_NODE));
            CheckMpv("observe paused-for-cache", mpv_observe_property(handle, 0, "paused-for-cache", MPV_FORMAT_FLAG));
            CheckMpv("observe display-swapchain",
                     mpv_observe_property(handle, 0, "display-swapchain", MPV_FORMAT_INT64));
            CheckMpv("observe eof-reached", mpv_observe_property(handle, 0, "eof-reached", MPV_FORMAT_FLAG));
            return handle;
        }
        catch (...)
        {
            mpv_terminate_destroy(handle);
            throw;
        }
    }

    namespace
    {
        // Each runs on the command worker, on the handle it was given.
        void SetFlag(mpv_handle* handle, char const* name, bool value)
        {
            int flag = value ? 1 : 0;
            CheckMpv("set flag property", mpv_set_property(handle, name, MPV_FORMAT_FLAG, &flag));
        }

        void SetDouble(mpv_handle* handle, char const* name, double value)
        {
            CheckMpv("set double property", mpv_set_property(handle, name, MPV_FORMAT_DOUBLE, &value));
        }

        void SetInt64(mpv_handle* handle, char const* name, std::int64_t value)
        {
            CheckMpv("set integer property", mpv_set_property(handle, name, MPV_FORMAT_INT64, &value));
        }

        void SetString(mpv_handle* handle, char const* name, std::string const& value)
        {
            CheckMpv("set string property", mpv_set_property_string(handle, name, value.c_str()));
        }
    }

    std::uint64_t MpvClient::Submit(MpvCommandKind kind, std::function<void(mpv_handle*)> call)
    {
        auto const generation = ++m_nextGeneration;
        auto* const handle = m_handle;
        if (!handle || m_stopping.load())
        {
            return generation;
        }
        auto const alive = m_dispatchAlive;
        auto const dispatcher = m_dispatcher;
        auto const handler = m_updateHandler;
        // Runs on the command worker and touches only what it captured. The
        // handle stays valid for it: Shutdown joins the worker before destroying
        // the handle. A failure is handed back to the UI thread as an update, and
        // only while this client is alive.
        auto job = [handle, call = std::move(call), kind, generation, alive, dispatcher, handler]()
        {
            try
            {
                call(handle);
                return;
            }
            catch (...)
            {
            }
            if (!alive->load())
            {
                return;
            }
            PlaybackUpdate update;
            update.FailedCommand = MpvCommandFailure{ kind, generation };
            try
            {
                static_cast<void>(dispatcher.TryEnqueue([alive, handler, update = std::move(update)]() mutable
                {
                    if (alive->load())
                    {
                        try { handler(std::move(update)); } catch (...) {}
                    }
                }));
            }
            catch (...)
            {
            }
        };
        static_cast<void>(m_calls.Enqueue(std::move(job)));
        return generation;
    }

    std::uint64_t MpvClient::Open(PlaybackSource const& source, TrackChoice trackChoice)
    {
        // Checked here, on the caller's thread, so a source that cannot be handed
        // to mpv at all still fails its caller at once, as it did when the load
        // itself ran on this thread.
        static_cast<void>(EncodeMpvUtf8(source.Location));
        static_cast<void>(EncodeMpvUtf8(SerializePlaybackHeaders(source.Headers)));
        std::optional<std::string> languages;
        if (trackChoice == TrackChoice::Automatic)
        {
            std::wstring joined;
            for (auto const& code : source.PreferredAudioLanguages)
            {
                if (!joined.empty()) joined.push_back(L',');
                joined += code;
            }
            try
            {
                languages = EncodeMpvUtf8(joined);
            }
            catch (std::exception const&)
            {
                languages = std::string{};
            }
        }
        return Submit(MpvCommandKind::Load, [source, languages](mpv_handle* handle)
        {
            if (languages)
            {
                // Track choices are per file. mpv keeps a chosen track number as
                // its setting, so the number picked for the last video would
                // select that number in this one, whatever language it is. Both go
                // back to automatic, and mpv is told the preferred audio language
                // so the right track plays from the first frame. Best effort, as
                // in the reference: the session still corrects the audio track
                // once the tracks are known.
                static_cast<void>(mpv_set_property_string(handle, "aid", "auto"));
                static_cast<void>(mpv_set_property_string(handle, "sid", "auto"));
                static_cast<void>(mpv_set_property_string(handle, "alang", languages->c_str()));
            }
            LoadMpvSource(handle, source);
        });
    }

    void MpvClient::SetPaused(bool paused)
    {
        static_cast<void>(Submit(MpvCommandKind::Other, [paused](mpv_handle* handle) { SetFlag(handle, "pause", paused); }));
    }

    std::uint64_t MpvClient::SeekAbsolute(double seconds)
    {
        return Submit(MpvCommandKind::Seek, [target = std::to_string(seconds)](mpv_handle* handle)
        {
            RunMpvCommand(handle, { "seek", target, "absolute" });
        });
    }

    std::uint64_t MpvClient::SeekRelative(double seconds)
    {
        return Submit(MpvCommandKind::Seek, [offset = std::to_string(seconds)](mpv_handle* handle)
        {
            RunMpvCommand(handle, { "seek", offset, "relative" });
        });
    }

    void MpvClient::SetVolume(double volume)
    {
        static_cast<void>(Submit(MpvCommandKind::Other, [volume](mpv_handle* handle) { SetDouble(handle, "volume", volume); }));
    }

    void MpvClient::SetSpeed(double speed)
    {
        static_cast<void>(Submit(MpvCommandKind::Other, [speed](mpv_handle* handle) { SetDouble(handle, "speed", speed); }));
    }

    void MpvClient::SetAudioTrack(std::int64_t id)
    {
        static_cast<void>(Submit(MpvCommandKind::Other, [id](mpv_handle* handle) { SetInt64(handle, "aid", id); }));
    }

    void MpvClient::SetSubtitleTrack(std::optional<std::int64_t> id)
    {
        static_cast<void>(Submit(MpvCommandKind::Subtitle, [id](mpv_handle* handle)
        {
            if (id)
            {
                SetInt64(handle, "sid", *id);
                return;
            }
            CheckMpv("disable subtitles", mpv_set_property_string(handle, "sid", "no"));
        }));
    }

    void MpvClient::SetSubtitleDelay(double seconds)
    {
        static_cast<void>(Submit(MpvCommandKind::Other, [seconds](mpv_handle* handle) { SetDouble(handle, "sub-delay", seconds); }));
    }

    void MpvClient::SetAudioDelay(double seconds)
    {
        static_cast<void>(Submit(MpvCommandKind::Other, [seconds](mpv_handle* handle) { SetDouble(handle, "audio-delay", seconds); }));
    }

    // A read rather than a command: answered at once by the player core, and
    // asked for by the resume and the seek clamp in the moment they decide.
    double MpvClient::DurationSeconds() const noexcept
    {
        double value{};
        return m_handle&&mpv_get_property(m_handle,"duration",MPV_FORMAT_DOUBLE,&value)>=0?value:0.0;
    }

    void MpvClient::AddExternalSubtitle(std::wstring const&path,std::wstring const&identity,std::wstring const&displayTitle,std::wstring const&language)
    {
        // `sub-add` parses the whole file before it returns, which is the call
        // most likely to take long; on the worker it no longer holds the window.
        std::vector<std::string> arguments{
            "sub-add",
            winrt::to_string(winrt::hstring(path)),
            "select",
            winrt::to_string(winrt::hstring(EncodeExternalSubtitleTrackTitle(identity,displayTitle))),
            winrt::to_string(winrt::hstring(language)),
        };
        static_cast<void>(Submit(MpvCommandKind::Subtitle, [arguments = std::move(arguments)](mpv_handle* handle)
        {
            RunMpvCommand(handle, arguments);
        }));
    }

    void MpvClient::RemoveTrack(std::int64_t id)
    {
        static_cast<void>(Submit(MpvCommandKind::Subtitle, [track = std::to_string(id)](mpv_handle* handle)
        {
            RunMpvCommand(handle, { "sub-remove", track });
        }));
    }

    void MpvClient::ApplySubtitleStyle(SubtitleStyle const&style)
    {
        // An empty face means "whatever mpv would have picked", so the property is left
        // alone rather than being set to an empty name mpv would fail to resolve.
        std::optional<std::string> font;
        if(!style.Font.empty())font=winrt::to_string(winrt::hstring(style.Font));
        static_cast<void>(Submit(MpvCommandKind::Other, [style, font](mpv_handle* handle)
        {
            SetDouble(handle,"sub-scale",style.Scale);
            if(font)SetString(handle,"sub-font",*font);
            SetDouble(handle,"sub-border-size",style.BorderSize);
            SetDouble(handle,"sub-shadow-offset",style.ShadowOffset);
            // scale is mpv's own default: a styled track keeps its presentation and only
            // follows sub-scale. force hands the whole presentation to the options above.
            if(!style.KeepTrackStyling)
            {
                SetString(handle,"sub-ass-override","force");
                return;
            }
            // An mpv older than `scale` (0.37) rejects it; `yes` is the nearest it knows.
            if(mpv_set_property_string(handle,"sub-ass-override","scale")<0)
            {
                SetString(handle,"sub-ass-override","yes");
            }
        }));
    }

    void MpvClient::SetVideoFit(VideoFitMode mode)
    {
        // panscan is a zoom factor toward full coverage rather than a flag: 0 leaves
        // the bars alone, 1 covers the window and crops what no longer fits. libmpv
        // derives the zoom from the video and window geometry itself, so this works
        // before any frame has been decoded.
        static_cast<void>(Submit(MpvCommandKind::Other, [mode](mpv_handle* handle)
        {
            SetDouble(handle, "panscan", mode == VideoFitMode::Fill ? 1.0 : 0.0);
        }));
    }

    void MpvClient::SetSurfaceSize(VideoSurfaceSize size)
    {
        // Changing this option at runtime makes libmpv resize the composition
        // swapchain; there is no window for it to watch.
        //
        // Still sent as an asynchronous request from the worker: a synchronous set
        // waits for the player core, which was measured blocking for over a tenth
        // of a second while the video output reconfigured, and that would hold up
        // every command queued behind it. libmpv copies the value and applies
        // queued requests in order, so the final size still wins.
        static_cast<void>(Submit(MpvCommandKind::SurfaceSize, [value = FormatSurfaceSize(size)](mpv_handle* handle) mutable
        {
            auto* raw = value.data();
            CheckMpv("set d3d11-composition-size",
                     mpv_set_property_async(handle, 0, "d3d11-composition-size", MPV_FORMAT_STRING, &raw));
        }));
    }

    void MpvClient::Shutdown() noexcept
    {
        if (!m_handle || m_stopping.exchange(true))
        {
            return;
        }

        // The order is the point, and nothing else is safe:
        // 1. No update, result or failure reaches the UI from here on.
        m_dispatchAlive->store(false);
        // 2. The command worker stops accepting calls, drops the ones not yet
        //    started, waits for the one in progress to return and is joined. From
        //    here nothing but this function calls into the handle.
        m_calls.Shutdown();
        // 3. quit, then the event thread is woken and joined, then the handle goes.
        char const* quitArguments[] = { "quit", nullptr };
        static_cast<void>(mpv_command(m_handle, quitArguments));
        // quit is asynchronous. Wake the blocking event wait even when mpv
        // accepted the command, otherwise an active network demuxer can leave
        // this thread asleep and make the UI hang forever in join().
        mpv_wakeup(m_handle);
        if (m_eventThread.joinable())
        {
            m_eventThread.join();
        }
        mpv_terminate_destroy(m_handle);
        m_handle = nullptr;
    }

    void MpvClient::EventLoop() noexcept
    {
        while (m_handle)
        {
            auto const event = mpv_wait_event(m_handle, -1.0);
            if (!event || ShouldExitMpvEventLoop(
                m_stopping.load(),
                event->event_id == MPV_EVENT_SHUTDOWN))
            {
                return;
            }
            if (event->event_id == MPV_EVENT_NONE)
            {
                continue;
            }

            try
            {
                auto update = TranslateEvent(*event);
                if (update)
                {
                    DispatchUpdate(std::move(*update));
                }
            }
            catch (...)
            {
            }
        }
    }

    void MpvClient::DispatchUpdate(PlaybackUpdate update) noexcept
    {
        try
        {
            auto const alive = m_dispatchAlive;
            auto const handler = m_updateHandler;
            static_cast<void>(m_dispatcher.TryEnqueue([alive, handler, update = std::move(update)]() mutable {
                if (alive->load())
                {
                    try{handler(std::move(update));}catch(...){}
                }
            }));
        }
        catch (...)
        {
        }
    }
} // namespace HaloDesktop::Playback
