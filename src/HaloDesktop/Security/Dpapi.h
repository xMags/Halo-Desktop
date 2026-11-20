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

    // The same read and write without the lock or the thread hop, for a caller
    // that already holds the file's Storage::FileMutationLock on this thread
    // (a named mutex, released only by the thread that took it). They do disk
    // and cryptography work on the calling thread, so never call them on a UI
    // thread.
    [[nodiscard]] std::optional<std::string> ReadProtectedTextLocked(
        std::filesystem::path const& path,
        std::uint64_t maximumBytes);
    void WriteProtectedTextLocked(
        std::filesystem::path const& path,
        std::string plaintext);
}
