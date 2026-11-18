#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <string>

namespace HaloDesktop::Security
{
    // The largest protected file a read accepts unless the caller says more.
    inline constexpr std::uint64_t DefaultMaximumProtectedFileBytes = 2u * 1024u * 1024u;

    // Thread-safe current-user DPAPI file helpers. Files are replaced
    // atomically, and no operation performs disk or cryptography work on the
    // caller's apartment.
    [[nodiscard]] concurrency::task<void> WriteProtectedTextAsync(
        std::filesystem::path path,
        std::string plaintext);

    // Empty when the file does not exist; std::length_error when it is larger
    // than maximumBytes.
    [[nodiscard]] concurrency::task<std::optional<std::string>> ReadProtectedTextAsync(
        std::filesystem::path path,
        std::uint64_t maximumBytes = DefaultMaximumProtectedFileBytes);

    [[nodiscard]] concurrency::task<void> DeleteProtectedFileAsync(
        std::filesystem::path path);
}
