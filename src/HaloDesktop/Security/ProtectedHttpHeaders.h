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
}
