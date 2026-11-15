#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace HaloDesktop::Security
{
    struct ProtectedHttpHeader final
    {
        std::wstring Name;
        std::wstring Value;

        bool operator==(ProtectedHttpHeader const&) const = default;
    };

    using ProtectedHttpHeaders = std::vector<ProtectedHttpHeader>;

    // Validates untrusted source headers before they cross into playback,
    // hashing, or download transports. Callers may add their own Range header
    // only after this validation succeeds.
    void ValidateProtectedHttpHeaders(ProtectedHttpHeaders const& headers);
    void ValidateProtectedHttpHeaders(
        std::map<std::wstring, std::wstring, std::less<>> const& headers);

    // Whether one header an addon asked for may be sent: a token name of at most
    // 128 characters that the player or the connection does not own, and a value
    // of at most 4096 characters that cannot break out of its line.
    [[nodiscard]] bool IsUsableProtectedHttpHeader(std::wstring const& name, std::wstring const& value);

    // The reference client's `playbackHeaders`: the first 64 headers, with every
    // unusable one dropped on its own so the stream still plays without it.
    [[nodiscard]] ProtectedHttpHeaders UsableProtectedHttpHeaders(ProtectedHttpHeaders const& headers);
}
