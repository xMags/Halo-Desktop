#include "pch.h"
#include "Playback/MpvClient.h"
#include "Services/PlaybackPreferences.h"

#include <mpv/client.h>

#include <algorithm>
#include <cwctype>
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
            if (title.empty())
            {
                title = language;
            }
            if (title.empty())
            {
                title = (trackType == HaloDesktop::Playback::TrackType::Audio ? L"Audio " : L"Subtitle ") +
                        std::to_wstring(*id);
            }

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
                ReadFlag(entry, "external"),
            });
        }
        return tracks;
    }

    std::optional<HaloDesktop::Playback::PlaybackUpdate> TranslateProperty(mpv_event const& event)
    {
        auto const property = static_cast<mpv_event_property const*>(event.data);
        if (!property || !property->name || property->format == MPV_FORMAT_NONE || !property->data)
        {
            return std::nullopt;
        }

        HaloDesktop::Playback::PlaybackUpdate update;
        auto const name = std::string_view(property->name);
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
            update.Ended = *static_cast<int const*>(property->data) != 0;
        }
        else if (name == "track-list" && property->format == MPV_FORMAT_NODE)
        {
            update.Tracks = ReadTracks(*static_cast<mpv_node const*>(property->data));
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
            else update.EndReason = HaloDesktop::Playback::PlaybackEndReason::Stopped;
            return update;
        }
        default:
            return std::nullopt;
        }
    }
} // namespace

namespace HaloDesktop::Playback
{
    MpvClient::MpvClient(std::uintptr_t videoWindowHandle,
                         winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher,
                         UpdateHandler updateHandler)
        : m_dispatcher(dispatcher), m_updateHandler(std::move(updateHandler))
    {
        if (videoWindowHandle == 0 || !m_dispatcher || !m_updateHandler)
        {
            throw std::invalid_argument("MpvClient requires a video window, dispatcher, and update handler");
        }

        int initializationError{};
        m_handle = CreateInitializedHandle(videoWindowHandle, "gpu-next", initializationError);
        if (!m_handle)
        {
            m_handle = CreateInitializedHandle(videoWindowHandle, "gpu", initializationError);
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

    mpv_handle* MpvClient::CreateInitializedHandle(std::uintptr_t videoWindowHandle, char const* videoOutput,
                                                   int& initializationError)
    {
        auto* handle = mpv_create();
        if (!handle)
        {
            throw std::runtime_error("mpv_create returned null");
        }

        try
        {
            auto windowId = static_cast<std::int64_t>(videoWindowHandle);
            CheckMpv("set wid", mpv_set_option(handle, "wid", MPV_FORMAT_INT64, &windowId));
            CheckMpv("set vo", mpv_set_option_string(handle, "vo", videoOutput));
            CheckMpv("set hwdec", mpv_set_option_string(
                handle,
                "hwdec",
                HaloDesktop::Services::PlaybackPreferences::HardwareDecodingEnabled() ? "auto-safe" : "no"));
            // keep-open suppresses the EOF end-file event on the retained last
            // frame. Up-next is deliberately driven only by that real event.
            CheckMpv("set idle", mpv_set_option_string(handle, "idle", "yes"));
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
            CheckMpv("observe eof-reached", mpv_observe_property(handle, 0, "eof-reached", MPV_FORMAT_FLAG));
            CheckMpv("observe paused-for-cache", mpv_observe_property(handle, 0, "paused-for-cache", MPV_FORMAT_FLAG));
            return handle;
        }
        catch (...)
        {
            mpv_terminate_destroy(handle);
            throw;
        }
    }

    void MpvClient::Open(std::wstring const& source)
    {
        Command({ "loadfile", winrt::to_string(winrt::hstring(source)), "replace" });
    }

    void MpvClient::SetPaused(bool paused)
    {
        auto value = paused ? 1 : 0;
        CheckMpv("set pause", mpv_set_property(m_handle, "pause", MPV_FORMAT_FLAG, &value));
    }

    void MpvClient::SeekAbsolute(double seconds)
    {
        Command({ "seek", std::to_string(seconds), "absolute" });
    }

    void MpvClient::SeekRelative(double seconds)
    {
        Command({ "seek", std::to_string(seconds), "relative" });
    }

    void MpvClient::SetVolume(double volume)
    {
        SetDoubleProperty("volume", volume);
    }

    void MpvClient::SetSpeed(double speed)
    {
        SetDoubleProperty("speed", speed);
    }

    void MpvClient::SetAudioTrack(std::int64_t id)
    {
        SetInt64Property("aid", id);
    }

    void MpvClient::SetSubtitleTrack(std::optional<std::int64_t> id)
    {
        if (id)
        {
            SetInt64Property("sid", *id);
            return;
        }
        CheckMpv("disable subtitles", mpv_set_property_string(m_handle, "sid", "no"));
    }

    void MpvClient::SetSubtitleDelay(double seconds)
    {
        SetDoubleProperty("sub-delay", seconds);
    }

    void MpvClient::SetAudioDelay(double seconds)
    {
        SetDoubleProperty("audio-delay", seconds);
    }

    double MpvClient::DurationSeconds() const noexcept
    {
        double value{};
        return m_handle&&mpv_get_property(m_handle,"duration",MPV_FORMAT_DOUBLE,&value)>=0?value:0.0;
    }
    void MpvClient::AddExternalSubtitle(std::wstring const&path,std::wstring const&identityTitle){Command({"sub-add",winrt::to_string(winrt::hstring(path)),"select",winrt::to_string(winrt::hstring(identityTitle))});}
    void MpvClient::RemoveTrack(std::int64_t id){Command({"sub-remove",std::to_string(id)});}
    void MpvClient::ApplySubtitleStyle(SubtitleStyle const&style){SetDoubleProperty("sub-scale",style.Scale);SetStringProperty("sub-font",style.Font);SetDoubleProperty("sub-border-size",style.BorderSize);SetDoubleProperty("sub-shadow-offset",style.ShadowOffset);}

    void MpvClient::Shutdown() noexcept
    {
        if (!m_handle || m_stopping.exchange(true))
        {
            return;
        }

        m_dispatchAlive->store(false);
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
            if (!event || event->event_id == MPV_EVENT_SHUTDOWN)
            {
                return;
            }
            if (event->event_id == MPV_EVENT_NONE && m_stopping.load())
            {
                return;
            }
            if (m_stopping.load())
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

    void MpvClient::Command(std::vector<std::string> const& arguments)
    {
        std::vector<char const*> values;
        values.reserve(arguments.size() + 1);
        for (auto const& argument : arguments)
        {
            values.push_back(argument.c_str());
        }
        values.push_back(nullptr);
        CheckMpv("mpv command", mpv_command(m_handle, values.data()));
    }

    void MpvClient::SetDoubleProperty(char const* name, double value)
    {
        CheckMpv("set double property", mpv_set_property(m_handle, name, MPV_FORMAT_DOUBLE, &value));
    }

    void MpvClient::SetInt64Property(char const* name, std::int64_t value)
    {
        CheckMpv("set integer property", mpv_set_property(m_handle, name, MPV_FORMAT_INT64, &value));
    }
    void MpvClient::SetStringProperty(char const*name,std::wstring const&value){CheckMpv("set string property",mpv_set_property_string(m_handle,name,winrt::to_string(winrt::hstring(value)).c_str()));}
} // namespace HaloDesktop::Playback
