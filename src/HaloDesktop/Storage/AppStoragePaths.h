#pragma once

#include <filesystem>

namespace HaloDesktop::Storage
{
    // Immutable application-owned paths. The default root is resolved with the
    // Windows known-folder API, never from a caller-controlled environment value.
    class AppStoragePaths final
    {
    public:
        AppStoragePaths();
        explicit AppStoragePaths(std::filesystem::path root);

        void EnsureDirectories() const;

        [[nodiscard]] std::filesystem::path const& Root() const noexcept;
        [[nodiscard]] std::filesystem::path const& LocalState() const noexcept;
        [[nodiscard]] std::filesystem::path const& Downloads() const noexcept;
        [[nodiscard]] std::filesystem::path const& TemporarySubtitles() const noexcept;
        [[nodiscard]] std::filesystem::path const& PreferencesFile() const noexcept;
        [[nodiscard]] std::filesystem::path const& MigrationMarker() const noexcept;
        [[nodiscard]] std::filesystem::path const& MigrationStaging() const noexcept;

    private:
        std::filesystem::path m_root;
        std::filesystem::path m_localState;
        std::filesystem::path m_downloads;
        std::filesystem::path m_temporarySubtitles;
        std::filesystem::path m_preferencesFile;
        std::filesystem::path m_migrationMarker;
        std::filesystem::path m_migrationStaging;
    };
}
