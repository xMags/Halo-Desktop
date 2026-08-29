#pragma once

#include <cstdint>

namespace HaloDesktop::Services
{
    // A load may only replace the in-memory document when it was issued against
    // the same local-write generation and the server has a strictly newer
    // document timestamp. Equal timestamps deliberately retain the local copy.
    [[nodiscard]] bool ShouldApplyLoadedSettings(
        std::int64_t payloadUpdatedAt,
        std::int64_t localUpdatedAt,
        std::uint64_t requestWriteVersion,
        std::uint64_t currentWriteVersion) noexcept;
}
