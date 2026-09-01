#include "pch.h"
#include "Services/PlaybackPreferences.h"

#include "Services/DevicePreferencesStore.h"

#include <stdexcept>
#include <utility>

namespace HaloDesktop::Services
{
    PlaybackPreferences::PlaybackPreferences(std::shared_ptr<DevicePreferencesStore> store)
        : m_store(std::move(store))
    {
        if (!m_store)
        {
            throw std::invalid_argument{ "PlaybackPreferences requires a device preference store." };
        }
    }

    bool PlaybackPreferences::ResumeEnabled() const noexcept
    {
        return m_store->ResumePlayback();
    }

    void PlaybackPreferences::ResumeEnabled(bool value)
    {
        m_store->ResumePlayback(value);
    }

    bool PlaybackPreferences::HardwareDecodingEnabled() const noexcept
    {
        return m_store->HardwareDecoding();
    }

    void PlaybackPreferences::HardwareDecodingEnabled(bool value)
    {
        m_store->HardwareDecoding(value);
    }

    bool PlaybackPreferences::DiscordPresenceEnabled() const noexcept
    {
        return m_store->DiscordPresence();
    }

    void PlaybackPreferences::DiscordPresenceEnabled(bool value)
    {
        m_store->DiscordPresence(value);
    }
}
