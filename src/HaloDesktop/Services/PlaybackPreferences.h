#pragma once

namespace HaloDesktop::Services
{
    // Device-local playback choices that must be available before synced
    // settings finish loading.
    class PlaybackPreferences final
    {
    public:
        [[nodiscard]] static bool ResumeEnabled() noexcept;
        static void ResumeEnabled(bool value);
        [[nodiscard]] static bool HardwareDecodingEnabled() noexcept;
        static void HardwareDecodingEnabled(bool value);
    };
}
