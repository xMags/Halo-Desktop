#pragma once

#include "Services/Downloads/DownloadTypes.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
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

        [[nodiscard]] std::vector<DownloadRecord> Load();
        void Save(std::vector<DownloadRecord> const& records, std::uint64_t generation);

        [[nodiscard]] std::filesystem::path DownloadDirectory() const;
        void SetDownloadDirectory(std::filesystem::path directory);
        [[nodiscard]] DownloadStoragePaths const& Paths() const noexcept;

    private:
        DownloadStoragePaths m_paths;
        mutable std::mutex m_mutex;
        std::filesystem::path m_downloadDirectory;
        std::uint64_t m_savedGeneration{};
    };
}
