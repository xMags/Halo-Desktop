#include "pch.h"
#include "Services/PlaybackPreferences.h"

#include <winrt/Windows.Storage.h>

namespace
{
    constexpr wchar_t ResumeKey[] = L"halo.resumePlayback.v1";
    constexpr wchar_t HardwareDecodingKey[] = L"halo.hardwareDecoding.v1";

    bool ReadBoolean(wchar_t const* key, bool defaultValue) noexcept
    {
        try
        {
            auto const value = winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values().TryLookup(key);
            return value ? winrt::unbox_value_or<bool>(value, defaultValue) : defaultValue;
        }
        catch (...)
        {
            return defaultValue;
        }
    }
}

namespace HaloDesktop::Services
{
    bool PlaybackPreferences::ResumeEnabled() noexcept
    {
        return ReadBoolean(ResumeKey, true);
    }

    void PlaybackPreferences::ResumeEnabled(bool value)
    {
        winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values().Insert(ResumeKey,winrt::box_value(value));
    }

    bool PlaybackPreferences::HardwareDecodingEnabled() noexcept
    {
        return ReadBoolean(HardwareDecodingKey, true);
    }

    void PlaybackPreferences::HardwareDecodingEnabled(bool value)
    {
        winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values().Insert(
            HardwareDecodingKey,
            winrt::box_value(value));
    }
}
