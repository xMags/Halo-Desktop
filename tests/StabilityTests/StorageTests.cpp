#include "StorageTests.h"

#include "Services/DevicePreferencesStore.h"
#include "Services/Downloads/DownloadIndexStore.h"
#include "Services/Downloads/TransferEngine.h"
#include "Storage/AppStoragePaths.h"
#include "Storage/FileStorage.h"
#include "Storage/LegacyPackageDataSource.h"
#include "Storage/PackagedDataMigrator.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <winrt/Windows.Data.Json.h>

namespace
{
    using HaloDesktop::Services::DevicePreferencesStore;
    using HaloDesktop::Services::Downloads::DownloadIndexStore;
    using HaloDesktop::Services::Downloads::DownloadMedia;
    using HaloDesktop::Services::Downloads::DownloadRecord;
    using HaloDesktop::Services::Downloads::DownloadStatus;
    using HaloDesktop::Services::Downloads::TransferEngine;
    using HaloDesktop::Storage::AppStoragePaths;
    using HaloDesktop::Storage::LegacyPackageData;
    using HaloDesktop::Storage::LegacyPackageDataSource;
    using HaloDesktop::Storage::MigrationResult;
    using HaloDesktop::Storage::PackagedDataMigrator;

    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    class TemporaryDirectory final
    {
    public:
        explicit TemporaryDirectory(std::wstring_view prefix = L"HaloDesktop-Storage-")
        {
            GUID id{};
            winrt::check_hresult(CoCreateGuid(&id));
            std::array<wchar_t, 40> text{};
            if (StringFromGUID2(id, text.data(), static_cast<int>(text.size())) == 0)
            {
                throw std::runtime_error{ "The storage test could not create an identifier." };
            }
            m_path = std::filesystem::temp_directory_path()
                / (std::wstring{ prefix } + text.data());
            if (!std::filesystem::create_directory(m_path))
            {
                throw std::runtime_error{ "The storage test could not create its temporary root." };
            }
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }

        TemporaryDirectory(TemporaryDirectory const&) = delete;
        TemporaryDirectory& operator=(TemporaryDirectory const&) = delete;

        [[nodiscard]] std::filesystem::path const& Path() const noexcept { return m_path; }

    private:
        std::filesystem::path m_path;
    };

    void WriteBytes(std::filesystem::path const& path, std::vector<std::uint8_t> const& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output{ path, std::ios::binary | std::ios::trunc };
        output.write(
            reinterpret_cast<char const*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!output)
        {
            throw std::runtime_error{ "The storage test could not write bytes." };
        }
    }

    std::vector<std::uint8_t> ReadBytes(std::filesystem::path const& path)
    {
        std::ifstream input{ path, std::ios::binary };
        if (!input)
        {
            throw std::runtime_error{ "The storage test could not read bytes." };
        }
        return {
            std::istreambuf_iterator<char>{ input },
            std::istreambuf_iterator<char>{},
        };
    }

    class FakeLegacySource final : public LegacyPackageDataSource
    {
    public:
        std::optional<LegacyPackageData> Value;
        int FailuresRemaining{};
        int ReadCount{};

        [[nodiscard]] std::optional<LegacyPackageData> Read() override
        {
            ++ReadCount;
            if (FailuresRemaining > 0)
            {
                --FailuresRemaining;
                throw std::runtime_error{ "simulated transient migration failure" };
            }
            return Value;
        }
    };

    void TestPreferenceValidationAndConcurrentMerge()
    {
        TemporaryDirectory temporary;
        auto const path = temporary.Path() / L"preferences.json";
        HaloDesktop::Storage::WriteUtf8FileAtomic(path, R"({"version":1,"theme":99,"resumePlayback":"wrong"})");
        DevicePreferencesStore first{ path };
        DevicePreferencesStore second{ path };
        Require(first.Theme() == 2, "an invalid theme did not fall back to System");
        Require(first.ResumePlayback(), "an invalid resume value did not use its default");

        std::atomic_bool start{};
        std::jthread themeWriter([&]()
        {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            first.Theme(1);
        });
        std::jthread resumeWriter([&]()
        {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            second.ResumePlayback(false);
        });
        start.store(true, std::memory_order_release);
        themeWriter.join();
        resumeWriter.join();

        DevicePreferencesStore verify{ path };
        Require(verify.Theme() == 1, "a concurrent preference write lost the theme key");
        Require(!verify.ResumePlayback(), "a concurrent preference write lost the resume key");
        verify.SearchHistory({ L"one", L"two" });
        Require(verify.SearchHistory().size() == 2, "valid search history did not round-trip");
    }

    void TestMigrationNoSource()
    {
        TemporaryDirectory temporary;
        auto paths = std::make_shared<AppStoragePaths>(temporary.Path() / L"standalone");
        auto source = std::make_shared<FakeLegacySource>();
        PackagedDataMigrator migrator{ paths, source };
        Require(migrator.Migrate() == MigrationResult::NoSource, "a missing legacy package was not handled");
        Require(std::filesystem::is_regular_file(paths->MigrationMarker()), "no-source migration was not marked complete");
        Require(migrator.Migrate() == MigrationResult::AlreadyComplete, "a completed no-source migration was repeated");
    }

    void TestMigrationSuccessRetryAndIdempotency()
    {
        TemporaryDirectory temporary;
        auto const legacy = temporary.Path() / L"legacy";
        std::filesystem::create_directories(legacy / L"downloads");
        std::vector<std::uint8_t> const protectedBytes{ 0, 1, 2, 3, 0xff, 0x42 };
        WriteBytes(legacy / L"auth-session.bin", protectedBytes);
        WriteBytes(legacy / L"downloads" / L"movie.partial", { 7, 8, 9 });

        auto paths = std::make_shared<AppStoragePaths>(temporary.Path() / L"standalone");
        auto source = std::make_shared<FakeLegacySource>();
        source->FailuresRemaining = 1;
        source->Value = LegacyPackageData{ .LocalState = legacy };
        source->Value->Preferences.Theme = 0;
        source->Value->Preferences.ResumePlayback = false;
        source->Value->Preferences.SearchHistory = { L"Halo" };
        PackagedDataMigrator migrator{ paths, source };

        Require(migrator.Migrate() == MigrationResult::RetryableFailure, "an interrupted migration was not retryable");
        Require(!std::filesystem::exists(paths->LocalState()), "an interrupted migration published partial data");
        Require(!std::filesystem::exists(paths->MigrationMarker()), "an interrupted migration was marked complete");
        paths->EnsureDirectories();
        Require(migrator.Migrate() == MigrationResult::Migrated, "a transient migration did not succeed on retry");
        Require(ReadBytes(paths->LocalState() / L"auth-session.bin") == protectedBytes,
            "migration changed DPAPI-protected session bytes");
        Require(ReadBytes(paths->Downloads() / L"movie.partial") == std::vector<std::uint8_t>({ 7, 8, 9 }),
            "migration did not preserve a download file");
        DevicePreferencesStore preferences{ paths->PreferencesFile() };
        Require(preferences.Theme() == 0 && !preferences.ResumePlayback(),
            "known legacy preferences were not imported");
        Require(preferences.SearchHistory() == std::vector<winrt::hstring>{ L"Halo" },
            "legacy search history was not imported");

        WriteBytes(paths->LocalState() / L"auth-session.bin", { 4, 5, 6 });
        WriteBytes(legacy / L"auth-session.bin", { 9, 9, 9 });
        Require(migrator.Migrate() == MigrationResult::AlreadyComplete, "an idempotent migration did not stop at its marker");
        Require(ReadBytes(paths->LocalState() / L"auth-session.bin") == std::vector<std::uint8_t>({ 4, 5, 6 }),
            "an idempotent migration overwrote standalone data");
    }

    void TestMigrationProtectsExistingTarget()
    {
        TemporaryDirectory temporary;
        auto paths = std::make_shared<AppStoragePaths>(temporary.Path() / L"standalone");
        std::filesystem::create_directories(paths->LocalState());
        WriteBytes(paths->LocalState() / L"sentinel.bin", { 1, 3, 3, 7 });
        auto source = std::make_shared<FakeLegacySource>();
        PackagedDataMigrator migrator{ paths, source };
        Require(migrator.Migrate() == MigrationResult::ExistingDataPreserved,
            "existing standalone data was not protected");
        Require(source->ReadCount == 0, "migration opened legacy data after finding an existing target");
        Require(ReadBytes(paths->LocalState() / L"sentinel.bin") == std::vector<std::uint8_t>({ 1, 3, 3, 7 }),
            "existing standalone data changed during migration");
    }

    DownloadRecord Record(std::wstring jobId, std::wstring videoId)
    {
        return {
            .JobId = std::move(jobId),
            .AccountKey = std::wstring(64, L'a'),
            .Media = DownloadMedia{
                .VideoId = std::move(videoId),
                .ItemId = L"movie:test",
                .MediaType = L"movie",
                .Title = L"Test",
            },
            .FileName = L"test.mkv",
            .RootPath = std::filesystem::temp_directory_path(),
            .Status = DownloadStatus::Paused,
            .SourceFingerprint = std::wstring(64, L'b'),
            .ExplicitPause = true,
            .CreatedAt = 1,
            .UpdatedAt = 1,
        };
    }

    void TestDownloadLeasesAndAtomicIndexMerge()
    {
        TemporaryDirectory temporary;
        auto const leasePath = temporary.Path() / L"job-lease";
        {
            HaloDesktop::Storage::FileMutationLock const first{ leasePath };
            std::atomic_bool excluded{};
            std::jthread competitor([&]()
            {
                try
                {
                    HaloDesktop::Storage::FileMutationLock const second{
                        leasePath, std::chrono::milliseconds{ 0 } };
                }
                catch (...)
                {
                    excluded.store(true, std::memory_order_release);
                }
            });
            competitor.join();
            Require(excluded.load(std::memory_order_acquire),
                "two processes could acquire the same download job lease");
        }
        HaloDesktop::Storage::FileMutationLock const reacquired{
            leasePath, std::chrono::milliseconds{ 0 } };

        auto const dataRoot = temporary.Path() / L"index";
        DownloadIndexStore first{ dataRoot };
        DownloadIndexStore second{ dataRoot };
        Require(first.Load().empty() && second.Load().empty(), "a new download index was not empty");
        first.Save({ Record(L"job-a", L"movie:a") }, 1);
        second.Save({ Record(L"job-b", L"movie:b") }, 1);
        DownloadIndexStore verify{ dataRoot };
        auto const records = verify.Load();
        Require(records.size() == 2, "a locked index update lost another process's new job");

        static_cast<void>(first.Load());
        static_cast<void>(second.Load());
        first.Save({ Record(L"job-b", L"movie:b") }, 2);
        second.Save({ Record(L"job-a", L"movie:a"), Record(L"job-b", L"movie:b") }, 2);
        DownloadIndexStore verifyDeletion{ dataRoot };
        auto const afterDeletion = verifyDeletion.Load();
        Require(afterDeletion.size() == 1 && afterDeletion.front().JobId == L"job-b",
            "a stale process resurrected a job deleted by another process");

        auto updated = Record(L"job-b", L"movie:b");
        updated.UpdatedAt = 2;
        first.Apply({ updated }, { L"job-a" });
        auto const afterApply = verifyDeletion.Load();
        Require(afterApply.size() == 1
                && afterApply.front().JobId == L"job-b"
                && afterApply.front().UpdatedAt == 2,
            "a record-level download index update was not atomic");
    }

    void TestPendingDownloadDeletionRecovery()
    {
        TemporaryDirectory temporary;
        auto const dataRoot = temporary.Path() / L"state";
        auto const downloadRoot = temporary.Path() / L"downloads";
        std::filesystem::create_directories(downloadRoot);

        auto record = Record(std::wstring(64, L'a'), L"movie:pending-delete");
        record.AccountKey = HaloDesktop::Services::Downloads::MakeAccountKey(
            L"https://example.test", L"user-a");
        record.RootPath = downloadRoot;
        record.FileName = L"pending.mkv";
        record.SubtitleFileName = L"pending.en.srt";
        record.PendingDeletion = true;
        WriteBytes(record.TargetPath(), { 1, 2, 3 });
        WriteBytes(record.PartialPath(), { 4, 5, 6 });
        WriteBytes(record.RootPath / *record.SubtitleFileName, { 7, 8, 9 });

        DownloadIndexStore store{ dataRoot };
        store.Apply({ record });
        {
            TransferEngine engine{ dataRoot };
            engine.SetAccount(L"https://example.test", L"user-a");
            Require(engine.List().empty(), "a pending deletion became visible after restart");
        }
        Require(!std::filesystem::exists(record.TargetPath())
                && !std::filesystem::exists(record.PartialPath())
                && !std::filesystem::exists(record.RootPath / *record.SubtitleFileName),
            "pending download files were not removed after restart");
        DownloadIndexStore verify{ dataRoot };
        Require(verify.Load(false).empty(), "a completed deletion tombstone remained in the index");
    }
}

void RunStandaloneStorageTests()
{
    TestPreferenceValidationAndConcurrentMerge();
    TestMigrationNoSource();
    TestMigrationSuccessRetryAndIdempotency();
    TestMigrationProtectsExistingTarget();
    TestDownloadLeasesAndAtomicIndexMerge();
    TestPendingDownloadDeletionRecovery();
}
