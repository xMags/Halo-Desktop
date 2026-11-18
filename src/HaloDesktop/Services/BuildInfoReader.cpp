#include "pch.h"
#include "Services/BuildInfoReader.h"

#include "Playback/MpvVersionProbe.h"
#include "Services/BuildInfoPolicy.h"

#include <windows.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace
{
    // The version resource HaloDesktop.rc compiles into the executable. It is
    // mapped with the image, so reading it is a memory read, not file work.
    std::optional<std::wstring> ReadAppVersion() noexcept
    {
        try
        {
            auto* const module = GetModuleHandleW(nullptr);
            auto* const found = FindResourceW(module, MAKEINTRESOURCEW(1), RT_VERSION);
            if (!found)
            {
                return std::nullopt;
            }
            auto const size = SizeofResource(module, found);
            auto* const loaded = LoadResource(module, found);
            if (!loaded || size == 0)
            {
                return std::nullopt;
            }
            auto const* const bytes = static_cast<std::uint8_t const*>(LockResource(loaded));
            if (!bytes)
            {
                return std::nullopt;
            }
            return ::HaloDesktop::Services::ParseFixedFileVersion(
                std::span<std::uint8_t const>{ bytes, static_cast<std::size_t>(size) });
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
}

namespace HaloDesktop::Services
{
    BuildInfo ReadBuildInfo() noexcept
    {
        BuildInfo info;
        info.App = ReadAppVersion();
        try
        {
            auto const raw = ::HaloDesktop::Playback::ReadLibmpvVersion();
            info.Mpv = MpvRelease(raw ? std::optional<std::string_view>{ *raw } : std::nullopt);
        }
        catch (...)
        {
            info.Mpv.reset();
        }
        return info;
    }
}
