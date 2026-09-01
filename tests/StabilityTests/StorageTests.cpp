#include "StorageTests.h"

#include "Services/DevicePreferencesStore.h"
#include "Services/Downloads/DownloadIndexStore.h"
#include "Services/Downloads/TransferEngine.h"
#include "Storage/AppStoragePaths.h"
#include "Storage/FileStorage.h"
#include "Storage/LegacyPackageDataSource.h"
#include "Storage/PackagedDataMigrator.h"

#include <windows.h>

#include <algorithm>
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
#include <winrt/Windows.Foundation.Collections.h>

namespace
{
    using HaloDesktop::Services::DevicePreferencesStore;
    using HaloDesktop::Services::Downloads::DownloadIndexStore;
    using HaloDesktop::Services::Downloads::DownloadMedia;
    using HaloDesktop::Services::Downloads::DownloadRecord;
    using HaloDesktop::Services::Downloads::DownloadStatus;
    using HaloDesktop::Services::Downloads::ReplacementBackup;
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
        Require(first.DiscordPresence(), "an invalid Discord presence value did not use its default");

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
        verify.DiscordPresence(false);
        Require(!DevicePreferencesStore{ path }.DiscordPresence(),
            "the Discord presence preference did not round-trip locally");
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

    std::vector<std::filesystem::path> QuarantinedIndexes(std::filesystem::path const& dataRoot)
    {
        std::vector<std::filesystem::path> result;
        for (auto const& entry : std::filesystem::directory_iterator{ dataRoot })
        {
            auto const name = entry.path().filename().wstring();
            if (entry.is_regular_file() && name.starts_with(L"downloads-index.corrupt-")
                && name.ends_with(L".json"))
            {
                result.push_back(entry.path());
            }
        }
        return result;
    }

    void TestCorruptDownloadIndexRecovery()
    {
        TemporaryDirectory temporary;
        auto const dataRoot = temporary.Path() / L"state";
        DownloadIndexStore store{ dataRoot };
        store.Save({ Record(L"job-a", L"movie:a"), Record(L"job-b", L"movie:b") }, 1);

        auto const indexPath = dataRoot / L"downloads-index.json";
        auto root = winrt::Windows::Data::Json::JsonObject::Parse(
            winrt::to_hstring(HaloDesktop::Storage::ReadUtf8File(indexPath, 32u * 1024u * 1024u)));
        auto const entries = root.GetNamedArray(L"entries");
        entries.GetObjectAt(1).Insert(
            L"status",
            winrt::Windows::Data::Json::JsonValue::CreateStringValue(L"corrupt"));
        auto const corrupt = winrt::to_string(root.Stringify());
        HaloDesktop::Storage::WriteUtf8FileAtomic(indexPath, corrupt);

        DownloadIndexStore recoveredStore{ dataRoot };
        auto const recovered = recoveredStore.Load(false);
        Require(recovered.size() == 1 && recovered.front().JobId == L"job-a",
            "a corrupt download entry prevented valid entries from being salvaged");
        auto quarantined = QuarantinedIndexes(dataRoot);
        Require(quarantined.size() == 1,
            "a corrupt download index was not preserved exactly once");
        Require(HaloDesktop::Storage::ReadUtf8File(quarantined.front(), 32u * 1024u * 1024u) == corrupt,
            "the quarantined download index did not preserve the corrupt input");
        Require(recoveredStore.Load(false).size() == 1 && QuarantinedIndexes(dataRoot).size() == 1,
            "the recovered download index was not stable on its next load");

        auto const malformedRoot = temporary.Path() / L"malformed";
        DownloadIndexStore malformedStore{ malformedRoot };
        auto const malformedPath = malformedRoot / L"downloads-index.json";
        HaloDesktop::Storage::WriteUtf8FileAtomic(malformedPath, "not-json");
        Require(malformedStore.Load(false).empty(),
            "a wholly malformed download index did not recover to an empty snapshot");
        auto const malformedQuarantine = QuarantinedIndexes(malformedRoot);
        Require(malformedQuarantine.size() == 1
                && HaloDesktop::Storage::ReadUtf8File(
                    malformedQuarantine.front(), 32u * 1024u * 1024u) == "not-json",
            "a wholly malformed download index was not quarantined");
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
        // Deletion is confined to folders this device has used for downloads, so
        // the fixture has to choose the folder its records claim to live in.
        store.SetDownloadDirectory(downloadRoot);
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

        auto old = Record(std::wstring(64, L'b'), L"movie:replacement-recovery");
        old.AccountKey = HaloDesktop::Services::Downloads::MakeAccountKey(
            L"https://example.test", L"user-a");
        old.RootPath = downloadRoot;
        old.FileName = L"old.mkv";
        old.PendingDeletion = true;
        auto replacement = Record(std::wstring(64, L'c'), L"movie:replacement-recovery");
        replacement.AccountKey = old.AccountKey;
        replacement.RootPath = downloadRoot;
        replacement.FileName = L"replacement.mkv";
        replacement.Status = DownloadStatus::Downloading;
        replacement.ExplicitPause = false;
        replacement.TotalBytes = 3;
        replacement.DownloadedBytes = 3;
        replacement.Replacement = ReplacementBackup{
            .JobId = old.JobId,
            .RootPath = old.RootPath,
            .FileName = old.FileName,
        };
        WriteBytes(old.TargetPath(), { 1, 2, 3 });
        WriteBytes(replacement.TargetPath(), { 4, 5, 6 });
        verify.Apply({ old, replacement });
        {
            TransferEngine engine{ dataRoot };
            engine.SetAccount(L"https://example.test", L"user-a");
            auto const records = engine.List();
            Require(records.size() == 1
                    && records.front().JobId == replacement.JobId
                    && records.front().Status == DownloadStatus::Done,
                "an interrupted replacement finalization was not recovered as complete");
        }
        Require(!std::filesystem::exists(old.TargetPath())
                && std::filesystem::is_regular_file(replacement.TargetPath()),
            "replacement recovery removed the wrong video file");
        auto const finalized = verify.Load(false);
        Require(finalized.size() == 1
                && finalized.front().JobId == replacement.JobId
                && finalized.front().Status == DownloadStatus::Done
                && !finalized.front().Replacement,
            "replacement recovery did not persist its terminal state");
    }

    void TestMissingCompletedDownloadRecovery()
    {
        TemporaryDirectory temporary;
        auto const dataRoot = temporary.Path() / L"state";
        DownloadIndexStore store{ dataRoot };
        store.SetDownloadDirectory(temporary.Path() / L"downloads");
        auto record = Record(std::wstring(64, L'd'), L"movie:missing");
        record.AccountKey = HaloDesktop::Services::Downloads::MakeAccountKey(
            L"https://example.test", L"user-a");
        record.RootPath = temporary.Path() / L"downloads";
        record.FileName = L"missing.mkv";
        record.Status = DownloadStatus::Done;
        record.ExplicitPause = false;
        store.Apply({ record });

        {
            TransferEngine engine{ dataRoot };
            engine.SetAccount(L"https://example.test", L"user-a");
            auto const records = engine.List();
            Require(records.size() == 1
                    && records.front().Status == DownloadStatus::Failed
                    && records.front().Failure
                    && *records.front().Failure == HaloDesktop::Services::Downloads::DownloadFailureCode::MissingFile,
                "a missing completed file was not reconciled as MissingFile");
            auto playbackRejected = false;
            try
            {
                static_cast<void>(engine.FilesForPlayback(record.JobId));
            }
            catch (...)
            {
                playbackRejected = true;
            }
            Require(playbackRejected, "playback accepted a missing completed file");
        }

        auto const persisted = store.Load(false);
        Require(persisted.size() == 1
                && persisted.front().Status == DownloadStatus::Failed
                && persisted.front().Failure
                && *persisted.front().Failure == HaloDesktop::Services::Downloads::DownloadFailureCode::MissingFile,
            "the MissingFile reconciliation was not persisted");
    }

    // The index is plain JSON on disk. A record that names a folder this device
    // never used for downloads must not be able to aim deletion or playback at
    // it, whether it got there by corruption or by an edit.
    void TestDownloadsOutsideApprovedRootsAreQuarantined()
    {
        TemporaryDirectory temporary;
        auto const dataRoot = temporary.Path() / L"state";
        auto const downloadRoot = temporary.Path() / L"downloads";
        auto const outsideRoot = temporary.Path() / L"elsewhere";
        std::filesystem::create_directories(downloadRoot);
        std::filesystem::create_directories(outsideRoot);

        auto const account = HaloDesktop::Services::Downloads::MakeAccountKey(
            L"https://example.test", L"user-a");
        auto doomed = Record(std::wstring(64, L'e'), L"movie:outside-delete");
        doomed.AccountKey = account;
        doomed.RootPath = outsideRoot;
        doomed.FileName = L"keep-me.mkv";
        doomed.PendingDeletion = true;
        WriteBytes(doomed.TargetPath(), { 1, 2, 3 });

        auto playable = Record(std::wstring(64, L'f'), L"movie:outside-play");
        playable.AccountKey = account;
        playable.RootPath = outsideRoot;
        playable.FileName = L"elsewhere.mkv";
        playable.Status = DownloadStatus::Done;
        playable.ExplicitPause = false;
        playable.TotalBytes = 3;
        playable.DownloadedBytes = 3;
        WriteBytes(playable.TargetPath(), { 4, 5, 6 });

        DownloadIndexStore store{ dataRoot };
        store.SetDownloadDirectory(downloadRoot);
        store.Apply({ doomed, playable });
        {
            TransferEngine engine{ dataRoot };
            engine.SetAccount(L"https://example.test", L"user-a");
            auto playbackRejected = false;
            try
            {
                static_cast<void>(engine.FilesForPlayback(playable.JobId));
            }
            catch (...)
            {
                playbackRejected = true;
            }
            Require(playbackRejected, "playback opened a file outside the approved download folders");
        }
        Require(std::filesystem::is_regular_file(doomed.TargetPath()),
            "a deletion outside the approved download folders removed a file");
        Require(std::filesystem::is_regular_file(playable.TargetPath()),
            "a record outside the approved download folders lost its file");

        DownloadIndexStore verify{ dataRoot };
        auto const remaining = verify.Load(false);
        Require(std::none_of(remaining.begin(), remaining.end(),
                [&doomed](DownloadRecord const& record) { return record.JobId == doomed.JobId; }),
            "an unsafe pending deletion stayed in the index to be retried forever");
    }
}

void RunStandaloneStorageTests()
{
    TestPreferenceValidationAndConcurrentMerge();
    TestMigrationNoSource();
    TestMigrationSuccessRetryAndIdempotency();
    TestMigrationProtectsExistingTarget();
    TestDownloadLeasesAndAtomicIndexMerge();
    TestCorruptDownloadIndexRecovery();
    TestPendingDownloadDeletionRecovery();
    TestMissingCompletedDownloadRecovery();
    TestDownloadsOutsideApprovedRootsAreQuarantined();
}
