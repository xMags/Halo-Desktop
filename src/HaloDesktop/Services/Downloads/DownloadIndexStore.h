#pragma once

#include "Services/Downloads/DownloadTypes.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace HaloDesktop::Services::Downloads
{
    struct DownloadStoragePaths final
    {
        std::filesystem::path DataRoot;
        std::filesystem::path IndexFile;
        std::filesystem::path ConfigFile;
        std::filesystem::path VaultDirectory;
        std::filesystem::path DefaultDownloadDirectory;
    };

    // Thread-safe atomic persistence for non-secret download metadata. Save
    // generations are monotonic, so a delayed older snapshot cannot replace a
    // newer state. This class never serializes a source URL or request header.
    class DownloadIndexStore final
    {
    public:
        explicit DownloadIndexStore(std::filesystem::path dataRoot);

        [[nodiscard]] std::vector<DownloadRecord> Load(bool recoverInterrupted = true);
        void Save(std::vector<DownloadRecord> const& records, std::uint64_t generation);
        void Apply(
            std::vector<DownloadRecord> const& upserts,
            std::vector<std::wstring> const& removals = {});
        [[nodiscard]] std::optional<DownloadRecord> SetLandscapeArtwork(
            std::wstring const& jobId,
            std::wstring const& expectedAccount,
            std::wstring artwork);

        [[nodiscard]] std::filesystem::path DownloadDirectory() const;
        void SetDownloadDirectory(std::filesystem::path directory);

        // Every folder this device has used as a download root, newest first.
        // The index itself is plain JSON that any corruption or edit can reach,
        // so file deletion is confined to these folders instead of trusting the
        // root path a record carries. The list is seeded once from the records
        // present at upgrade time, then only grows when a folder is chosen.
        [[nodiscard]] std::vector<std::filesystem::path> ApprovedRoots() const;
        [[nodiscard]] DownloadStoragePaths const& Paths() const noexcept;

    private:
        void WriteConfigLocked() const;

        DownloadStoragePaths m_paths;
        mutable std::mutex m_mutex;
        std::filesystem::path m_downloadDirectory;
        std::vector<std::filesystem::path> m_approvedRoots;
        std::set<std::wstring, std::less<>> m_observedJobIds;
        std::uint64_t m_savedGeneration{};
    };
}
