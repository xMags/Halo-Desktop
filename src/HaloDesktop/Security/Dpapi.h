#pragma once

#include <filesystem>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <string>

namespace HaloDesktop::Security
{
    // Thread-safe current-user DPAPI file helpers. Files are replaced
    // atomically, and no operation performs disk or cryptography work on the
    // caller's apartment.
    [[nodiscard]] concurrency::task<void> WriteProtectedTextAsync(
        std::filesystem::path path,
        std::string plaintext);

    [[nodiscard]] concurrency::task<std::optional<std::string>> ReadProtectedTextAsync(
        std::filesystem::path path);

    [[nodiscard]] concurrency::task<void> DeleteProtectedFileAsync(
        std::filesystem::path path);
}
