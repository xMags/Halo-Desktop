#include "pch.h"
#include "Playback/MpvVersionProbe.h"

#include <mpv/client.h>

#include <array>
#include <memory>
#include <utility>

namespace
{
    struct MpvHandleDeleter final
    {
        void operator()(mpv_handle* handle) const noexcept
        {
            mpv_terminate_destroy(handle);
        }
    };

    struct MpvStringDeleter final
    {
        void operator()(char* text) const noexcept
        {
            mpv_free(text);
        }
    };
}

namespace HaloDesktop::Playback
{
    std::optional<std::string> ReadLibmpvVersion() noexcept
    {
        try
        {
            std::unique_ptr<mpv_handle, MpvHandleDeleter> const handle{ mpv_create() };
            if (!handle)
            {
                return std::nullopt;
            }
            // Nothing is ever loaded into this instance; these keep it from
            // touching a device, the user's mpv config or any script. Each is
            // required: an instance that refused one is not started.
            constexpr std::array<std::pair<char const*, char const*>, 8> options{ {
                { "vo", "null" },
                { "ao", "null" },
                { "idle", "yes" },
                { "config", "no" },
                { "terminal", "no" },
                { "load-scripts", "no" },
                { "ytdl", "no" },
                { "input-default-bindings", "no" },
            } };
            for (auto const& [name, value] : options)
            {
                if (mpv_set_option_string(handle.get(), name, value) < 0)
                {
                    return std::nullopt;
                }
            }
            if (mpv_initialize(handle.get()) < 0)
            {
                return std::nullopt;
            }
            std::unique_ptr<char, MpvStringDeleter> const version{
                mpv_get_property_string(handle.get(), "mpv-version") };
            if (!version)
            {
                return std::nullopt;
            }
            return std::string{ version.get() };
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
}
