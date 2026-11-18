#pragma once

#include <optional>
#include <string>

namespace HaloDesktop::Services
{
    struct BuildInfo final
    {
        // The executable's version resource, as `1.0.0`.
        std::optional<std::wstring> App;
        // The loaded libmpv's release number, as `0.41.0`.
        std::optional<std::wstring> Mpv;
    };

    // Reads both. Blocking (it starts a throwaway libmpv instance), so it is
    // called off the UI thread. Never throws; a part that cannot be read is null.
    [[nodiscard]] BuildInfo ReadBuildInfo() noexcept;
}
