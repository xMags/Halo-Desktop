#pragma once

#include <memory>

namespace HaloDesktop::Services
{
    class DevicePreferencesStore;
}

namespace HaloDesktop::Storage
{
    class AppStoragePaths;
    class LegacyPackageDataSource;

    enum class MigrationResult
    {
        AlreadyComplete,
        Migrated,
        NoSource,
        ExistingDataPreserved,
        RetryableFailure,
    };

    // Synchronous by design. Migration finishes before any service opens the
    // standalone data tree, so no service can observe or overwrite a partial copy.
    class PackagedDataMigrator final
    {
    public:
        PackagedDataMigrator(
            std::shared_ptr<AppStoragePaths const> paths,
            std::shared_ptr<LegacyPackageDataSource> source);

        [[nodiscard]] MigrationResult Migrate() noexcept;

    private:
        std::shared_ptr<AppStoragePaths const> m_paths;
        std::shared_ptr<LegacyPackageDataSource> m_source;
    };
}
