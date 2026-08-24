#pragma once

#include <optional>

namespace HaloDesktop::Playback
{
    struct WindowsAudioSessionState final
    {
        double Volume{};
        bool Muted{};
    };

    // Process-local adapter for the Windows shared-mode audio session created
    // by libmpv. This keeps Halo's volume control aligned with the Volume Mixer
    // without changing the endpoint volume used by other applications.
    class WindowsAudioSession final
    {
    public:
        [[nodiscard]] static std::optional<WindowsAudioSessionState> Read() noexcept;
        [[nodiscard]] static bool SetVolume(double volume, bool unmute) noexcept;
    };
}
