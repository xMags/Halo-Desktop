#include "pch.h"
#include "Storage/AppStoragePaths.h"

#include <knownfolders.h>
#include <shlobj.h>
#include <stdexcept>
#include <system_error>
#include <wil/resource.h>

namespace
{
    std::filesystem::path CurrentUserLocalAppData()
    {
        PWSTR rawPath{};
        winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &rawPath));
        wil::unique_cotaskmem_string path{ rawPath };
        if (!path || path.get()[0] == L'\0')
        {
            throw std::runtime_error{ "Windows returned an empty local application data path." };
        }
        return std::filesystem::path{ path.get() };
    }

    void CreateDirectory(std::filesystem::path const& path)
    {
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error || !std::filesystem::is_directory(path))
        {
            throw std::system_error{
                error ? error : std::make_error_code(std::errc::not_a_directory),
                "Could not create a Halo data directory" };
        }
    }
}

namespace HaloDesktop::Storage
{
    AppStoragePaths::AppStoragePaths()
        : AppStoragePaths(CurrentUserLocalAppData() / L"Halo Desktop")
    {
    }

    AppStoragePaths::AppStoragePaths(std::filesystem::path root)
        : m_root(root.empty()
            ? std::filesystem::path{}
            : std::filesystem::absolute(root).lexically_normal()),
          m_localState(m_root / L"LocalState"),
          m_downloads(m_localState / L"downloads"),
          m_temporarySubtitles(m_root / L"Temp" / L"subtitles"),
          m_preferencesFile(m_localState / L"device-preferences.json"),
          m_migrationMarker(m_root / L"standalone-migration-v1.complete"),
          m_migrationStaging(m_root / L"MigrationStaging")
    {
        if (m_root.empty() || !m_root.is_absolute())
        {
            throw std::invalid_argument{ "An absolute Halo data root is required." };
        }
    }

    void AppStoragePaths::EnsureDirectories() const
    {
        CreateDirectory(m_localState);
        CreateDirectory(m_downloads);
        CreateDirectory(m_temporarySubtitles);
    }

    std::filesystem::path const& AppStoragePaths::Root() const noexcept { return m_root; }
    std::filesystem::path const& AppStoragePaths::LocalState() const noexcept { return m_localState; }
    std::filesystem::path const& AppStoragePaths::Downloads() const noexcept { return m_downloads; }
    std::filesystem::path const& AppStoragePaths::TemporarySubtitles() const noexcept { return m_temporarySubtitles; }
    std::filesystem::path const& AppStoragePaths::PreferencesFile() const noexcept { return m_preferencesFile; }
    std::filesystem::path const& AppStoragePaths::MigrationMarker() const noexcept { return m_migrationMarker; }
    std::filesystem::path const& AppStoragePaths::MigrationStaging() const noexcept { return m_migrationStaging; }
}
