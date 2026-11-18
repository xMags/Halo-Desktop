#pragma once

#include <optional>
#include <string>

namespace HaloDesktop::Playback
{
    // The loaded libmpv's own `mpv-version` string, read from a short-lived,
    // idle instance that opens no output and loads no scripts or config.
    // Blocking (it initialises and tears down that instance), so it is called
    // off the UI thread. Null when libmpv cannot be started or does not answer.
    [[nodiscard]] std::optional<std::string> ReadLibmpvVersion() noexcept;
}
