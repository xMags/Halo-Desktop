#include "pch.h"
#include "Services/SettingsSyncPolicy.h"

namespace HaloDesktop::Services
{
    bool ShouldApplyLoadedSettings(
        std::int64_t payloadUpdatedAt,
        std::int64_t localUpdatedAt,
        std::uint64_t requestWriteVersion,
        std::uint64_t currentWriteVersion) noexcept
    {
        return requestWriteVersion == currentWriteVersion
            && payloadUpdatedAt > localUpdatedAt;
    }
}
