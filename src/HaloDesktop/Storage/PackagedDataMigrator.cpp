#include "pch.h"
#include "Storage/PackagedDataMigrator.h"

#include "Services/DevicePreferencesStore.h"
#include "Storage/AppStoragePaths.h"
#include "Storage/FileStorage.h"
#include "Storage/LegacyPackageDataSource.h"

#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <wil/resource.h>

namespace
{
    bool IsReparsePoint(std::filesystem::path const& path)
    {
        auto const attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            throw std::system_error{
                static_cast<int>(GetLastError()),
                std::system_category(),
                "Could not inspect legacy Halo data" };
        }
        return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    }

    void RemoveStaging(std::filesystem::path const& staging) noexcept
    {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
    }

    void CopyLegacyState(
        std::filesystem::path const& source,
        std::filesystem::path const& destination)
    {
        if (source.empty() || !std::filesystem::is_directory(source) || IsReparsePoint(source))
        {
            throw std::runtime_error{ "The legacy Halo data root is unavailable or unsafe." };
        }
        std::filesystem::create_directories(destination);
        for (auto const& entry : std::filesystem::recursive_directory_iterator{
            source, std::filesystem::directory_options::none })
        {
            auto const& path = entry.path();
            if (IsReparsePoint(path))
            {
                throw std::runtime_error{ "Legacy Halo data contains a reparse point." };
            }
            auto const relative = path.lexically_relative(source);
            if (relative.empty() || relative.is_absolute()
                || *relative.begin() == std::filesystem::path{ L".." })
            {
                throw std::runtime_error{ "Legacy Halo data contains an unsafe path." };
            }
            auto const target = destination / relative;
            if (entry.is_directory())
            {
                std::filesystem::create_directories(target);
                continue;
            }
            if (!entry.is_regular_file())
            {
                throw std::runtime_error{ "Legacy Halo data contains an unsupported file type." };
            }
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(path, target, std::filesystem::copy_options::none);
        }
    }

    void MarkComplete(HaloDesktop::Storage::AppStoragePaths const& paths)
    {
        HaloDesktop::Storage::WriteUtf8FileAtomic(paths.MigrationMarker(), "1\n");
    }
}

namespace HaloDesktop::Storage
{
    PackagedDataMigrator::PackagedDataMigrator(
        std::shared_ptr<AppStoragePaths const> paths,
        std::shared_ptr<LegacyPackageDataSource> source)
        : m_paths(std::move(paths)), m_source(std::move(source))
    {
        if (!m_paths || !m_source)
        {
            throw std::invalid_argument{ "PackagedDataMigrator requires paths and a source." };
        }
    }

    MigrationResult PackagedDataMigrator::Migrate() noexcept
    {
        try
        {
            FileMutationLock const migrationLock{ m_paths->MigrationMarker() };
            if (std::filesystem::is_regular_file(m_paths->MigrationMarker()))
            {
                return MigrationResult::AlreadyComplete;
            }

            std::error_code createError;
            std::filesystem::create_directories(m_paths->Root(), createError);
            if (createError || IsReparsePoint(m_paths->Root()))
            {
                throw std::runtime_error{ "The standalone Halo data root is unavailable or unsafe." };
            }
            if (std::filesystem::exists(m_paths->LocalState()))
            {
                MarkComplete(*m_paths);
                return MigrationResult::ExistingDataPreserved;
            }

            auto const legacy = m_source->Read();
            if (!legacy)
            {
                MarkComplete(*m_paths);
                return MigrationResult::NoSource;
            }

            RemoveStaging(m_paths->MigrationStaging());
            auto cleanup = wil::scope_exit([this]() noexcept
            {
                RemoveStaging(m_paths->MigrationStaging());
            });
            auto const stagedState = m_paths->MigrationStaging() / L"LocalState";
            CopyLegacyState(legacy->LocalState, stagedState);
            ::HaloDesktop::Services::DevicePreferencesStore stagedPreferences{
                stagedState / L"device-preferences.json" };
            static_cast<void>(stagedPreferences.ImportIfMissing(legacy->Preferences));

            if (!MoveFileExW(
                stagedState.c_str(),
                m_paths->LocalState().c_str(),
                MOVEFILE_WRITE_THROUGH))
            {
                auto const error = GetLastError();
                if ((error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
                    && std::filesystem::exists(m_paths->LocalState()))
                {
                    MarkComplete(*m_paths);
                    return MigrationResult::ExistingDataPreserved;
                }
                throw std::system_error{
                    static_cast<int>(error),
                    std::system_category(),
                    "Could not promote migrated Halo data" };
            }
            MarkComplete(*m_paths);
            return MigrationResult::Migrated;
        }
        catch (...)
        {
            RemoveStaging(m_paths->MigrationStaging());
            OutputDebugStringW(L"Halo standalone data migration did not complete and remains retryable.\n");
            return MigrationResult::RetryableFailure;
        }
    }
}
