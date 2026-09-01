#pragma once

#include <memory>

namespace HaloDesktop::Services
{
    class DevicePreferencesStore;

    // Device-local playback choices that must be available before synced
    // settings finish loading.
    class PlaybackPreferences final
    {
    public:
        explicit PlaybackPreferences(std::shared_ptr<DevicePreferencesStore> store);

        [[nodiscard]] bool ResumeEnabled() const noexcept;
        void ResumeEnabled(bool value);
        [[nodiscard]] bool HardwareDecodingEnabled() const noexcept;
        void HardwareDecodingEnabled(bool value);
        [[nodiscard]] bool DiscordPresenceEnabled() const noexcept;
        void DiscordPresenceEnabled(bool value);

    private:
        std::shared_ptr<DevicePreferencesStore> m_store;
    };
}
