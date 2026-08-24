#include "pch.h"
#include "Services/PlaybackPreferences.h"

#include <winrt/Windows.Storage.h>

namespace
{
    constexpr wchar_t ResumeKey[] = L"halo.resumePlayback.v1";
}

namespace HaloDesktop::Services
{
    bool PlaybackPreferences::ResumeEnabled() noexcept
    {
        try
        {
            auto const value=winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values().TryLookup(ResumeKey);
            return value?winrt::unbox_value_or<bool>(value,true):true;
        }
        catch (...)
        {
            return true;
        }
    }

    void PlaybackPreferences::ResumeEnabled(bool value)
    {
        winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values().Insert(ResumeKey,winrt::box_value(value));
    }
}
