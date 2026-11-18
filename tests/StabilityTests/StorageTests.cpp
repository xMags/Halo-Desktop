#include "StorageTests.h"

#include "Security/Dpapi.h"
#include "Services/Auth/SessionStore.h"
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
#include <wil/resource.h>

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
        Require(!first.NavPaneOpen(), "the navigation pane did not start closed");

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
        verify.SearchHistory({ { L"one", 1700000000000 }, { L"two", 0 } });
        Require(verify.SearchHistory() == std::vector<HaloDesktop::Services::SearchHistoryEntry>{
                    { L"one", 1700000000000 }, { L"two", 0 } },
            "valid search history did not round-trip with its times");

        // The pane state is merged like every other key: writing it keeps the
        // rest, and a later write of another key keeps it.
        verify.NavPaneOpen(true);
        Require(DevicePreferencesStore{ path }.NavPaneOpen(), "the open navigation pane did not round-trip");
        Require(verify.Theme() == 1 && !verify.ResumePlayback(), "writing the pane state lost other keys");
        second.HardwareDecoding(false);
        Require(verify.NavPaneOpen(), "another preference write lost the pane state");
        verify.NavPaneOpen(false);
        Require(!DevicePreferencesStore{ path }.NavPaneOpen(), "the closed navigation pane did not round-trip");

        // The player's fill choice is kept per device and fits until chosen.
        Require(!verify.FillPicture(), "the picture did not fit by default");
        verify.FillPicture(true);
        Require(DevicePreferencesStore{ path }.FillPicture(), "the fill choice did not round-trip");
        Require(verify.Theme() == 1 && !verify.ResumePlayback(), "writing the fill choice lost other keys");
        verify.FillPicture(false);
        Require(!DevicePreferencesStore{ path }.FillPicture(), "the fit choice did not round-trip");

        // A value of the wrong kind falls back to closed rather than opening it.
        auto const wrongPath = temporary.Path() / L"wrong-pane.json";
        HaloDesktop::Storage::WriteUtf8FileAtomic(wrongPath, R"({"version":1,"navPaneOpen":"yes"})");
        Require(!DevicePreferencesStore{ wrongPath }.NavPaneOpen(), "an invalid pane value did not fall back to closed");
    }

    // A preferences file that cannot be read is never replaced by defaults:
    // the write is refused and the file stays as it was. A missing file, or
    // one that is read but holds no preferences, starts from the defaults.
    void TestUnreadablePreferencesAreNotReplaced()
    {
        TemporaryDirectory temporary;
        auto const path = temporary.Path() / L"preferences.json";
        std::string const original =
            R"({"version":1,"theme":0,"lastServerAddress":"saved.test","sessionServerUrl":"https://saved.test"})";
        HaloDesktop::Storage::WriteUtf8FileAtomic(path, original);
        DevicePreferencesStore store{ path };

        auto const refuses = [](auto const& call)
        {
            try
            {
                call();
            }
            catch (...)
            {
                return true;
            }
            return false;
        };
        {
            // Another program holding the file open without sharing.
            wil::unique_hfile held{ CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
            Require(static_cast<bool>(held), "the test could not hold the preferences file");
            Require(refuses([&store]() { store.Theme(1); }), "a theme was written over a file that could not be read");
            Require(refuses([&store]() { store.LastServerAddress(L"other.test"); }),
                "a server address was written over a file that could not be read");
        }
        auto const kept = ReadBytes(path);
        Require(std::string(kept.begin(), kept.end()) == original, "a preferences file that could not be read was changed");
        store.Theme(1);
        Require(store.Theme() == 1 && store.LastServerAddress() == L"saved.test" && store.SessionServerUrl() == L"https://saved.test",
            "the write after the file became readable again did not keep the other keys");

        // Over the size limit: refused, and left alone.
        std::string oversized = R"({"version":1,"lastServerAddress":"saved.test","pad":")";
        oversized.append(1024u * 1024u, 'x');
        oversized += R"("})";
        HaloDesktop::Storage::WriteUtf8FileAtomic(path, oversized);
        Require(refuses([&store]() { store.NavPaneOpen(true); }), "a preference was written over an oversized file");
        Require(ReadBytes(path).size() == oversized.size(), "an oversized preferences file was replaced");

        // Read, but not preferences: the defaults, as before.
        HaloDesktop::Storage::WriteUtf8FileAtomic(path, "not json");
        store.NavPaneOpen(true);
        Require(store.NavPaneOpen() && store.Theme() == 2, "a corrupt preferences file did not start again from the defaults");

        auto const missing = temporary.Path() / L"missing" / L"preferences.json";
        DevicePreferencesStore fresh{ missing };
        fresh.LastServerAddress(L"new.test");
        Require(DevicePreferencesStore{ missing }.LastServerAddress() == L"new.test", "a missing preferences file was not created");
    }

    // The subtitle memory is bounded in the bytes it takes in the file, so a
    // memory the store accepted always reads back.
    void TestSubtitleMemoryFitsTheFile()
    {
        TemporaryDirectory temporary;
        auto const path = temporary.Path() / L"preferences.json";
        DevicePreferencesStore store{ path };
        store.SearchHistory({ { std::wstring(120, L'\x1'), 1 } });

        // 400 000 characters, under the old character cap, but 1.2 MB once
        // written as UTF-8: a file the next read would refuse.
        winrt::Windows::Data::Json::JsonObject wide;
        wide.Insert(L"v", winrt::Windows::Data::Json::JsonValue::CreateStringValue(winrt::hstring(std::wstring(400000, L'\x4E2D'))));
        auto refused = false;
        try
        {
            store.SubtitleSelectionMemory(wide);
        }
        catch (std::invalid_argument const&)
        {
            refused = true;
        }
        Require(refused, "a subtitle memory too large for the file was accepted");

        winrt::Windows::Data::Json::JsonObject large;
        large.Insert(L"v", winrt::Windows::Data::Json::JsonValue::CreateStringValue(winrt::hstring(std::wstring(170000, L'\x4E2D'))));
        store.SubtitleSelectionMemory(large);
        store.LastServerAddress(std::wstring(512, L'\x4E2D'));
        Require(DevicePreferencesStore{ path }.SubtitleSelectionMemory().HasKey(L"v")
                && DevicePreferencesStore{ path }.LastServerAddress().size() == 512,
            "a subtitle memory the store accepted did not read back");
    }

    // The history became `{ term, at }` rows. A file an older build wrote
    // holds plain strings; the first run of this build must keep every one of
    // them, and the two shapes may sit side by side in one list.
    void TestSearchHistoryShapes()
    {
        using HaloDesktop::Services::SearchHistoryEntry;
        TemporaryDirectory temporary;
        auto const path = temporary.Path() / L"preferences.json";
        HaloDesktop::Storage::WriteUtf8FileAtomic(
            path, R"({"version":1,"theme":1,"searchHistory":["dune","the office"],"resumePlayback":false})");
        DevicePreferencesStore store{ path };
        Require(store.SearchHistory() == std::vector<SearchHistoryEntry>{ { L"dune", 0 }, { L"the office", 0 } },
            "an older build's plain search history was not read");

        // Another key's write keeps the old list as it was, now in the new shape.
        store.Theme(0);
        Require(store.SearchHistory() == std::vector<SearchHistoryEntry>{ { L"dune", 0 }, { L"the office", 0 } },
            "writing another preference lost the older search history");
        auto const rewritten = HaloDesktop::Storage::ReadUtf8File(path, 1024u * 1024u);
        Require(rewritten.find(R"("term":"dune")") != std::string::npos,
            "the search history was not written as term objects");
        Require(!store.ResumePlayback(), "reading the older history lost another key");

        HaloDesktop::Storage::WriteUtf8FileAtomic(
            path,
            R"({"version":1,"searchHistory":["old",{"term":"new","at":1731600000000},{"term":"no time"},)"
            R"({"term":"bad time","at":"soon"},{"term":"negative","at":-5},{"at":3},{"term":""},7,null,)"
            R"({"term":"fraction","at":12.75}]})");
        Require(DevicePreferencesStore{ path }.SearchHistory() == std::vector<SearchHistoryEntry>{
                    { L"old", 0 },
                    { L"new", 1731600000000 },
                    { L"no time", 0 },
                    { L"bad time", 0 },
                    { L"negative", 0 },
                    { L"fraction", 12 } },
            "a mixed search history was not read row by row");

        std::wstring const tooLong(121, L'x');
        HaloDesktop::Storage::WriteUtf8FileAtomic(
            path, R"({"version":1,"searchHistory":[")" + winrt::to_string(tooLong) + R"(",{"term":"kept","at":1}]})");
        Require(DevicePreferencesStore{ path }.SearchHistory() == std::vector<SearchHistoryEntry>{ { L"kept", 1 } },
            "a term longer than the box accepts was kept");
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
        source->Value->Preferences.SearchHistory = { { L"Halo", 0 } };
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
        Require(preferences.SearchHistory() == std::vector<HaloDesktop::Services::SearchHistoryEntry>{ { L"Halo", 0 } },
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

    void TestDownloadLandscapeArtworkPersistence()
    {
        TemporaryDirectory temporary;
        DownloadIndexStore store{ temporary.Path() / L"state" };
        auto record = Record(L"job-art", L"series:art:1:2");
        record.Media.MediaType = L"series";
        record.Media.MetaId = L"series:art";
        store.Save({ record }, 1);

        Require(
            !store.SetLandscapeArtwork(record.JobId, L"another-account", L"wrong.jpg"),
            "landscape artwork crossed the download account boundary");
        auto const updated = store.SetLandscapeArtwork(
            record.JobId,
            record.AccountKey,
            L"https://images.example.test/episode.jpg");
        Require(updated
                && updated->Media.LandscapeArtwork
                && *updated->Media.LandscapeArtwork == L"https://images.example.test/episode.jpg",
            "landscape artwork was not added to the persisted record");
        auto const repeated = store.SetLandscapeArtwork(
            record.JobId,
            record.AccountKey,
            L"https://images.example.test/replacement.jpg");
        Require(repeated
                && repeated->Media.LandscapeArtwork
                && *repeated->Media.LandscapeArtwork == L"https://images.example.test/episode.jpg",
            "a concurrent resolver replaced or failed to adopt persisted landscape artwork");

        // This is the shape of a progress snapshot copied before enrichment.
        // Applying it later must not erase the independently resolved artwork.
        record.DownloadedBytes = 512;
        record.UpdatedAt = 2;
        store.Apply({ record });
        auto const loaded = store.Load(false);
        Require(loaded.size() == 1
                && loaded.front().DownloadedBytes == 512
                && loaded.front().Media.LandscapeArtwork
                && *loaded.front().Media.LandscapeArtwork == L"https://images.example.test/episode.jpg",
            "a stale progress record erased persisted landscape artwork");
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

namespace
{
    // The saved identity carries what the last /auth/me said about the account,
    // so the account caption is right at launch. A file the previous version
    // wrote, or one whose new fields do not parse, still restores the account.
    void TestSavedIdentityProfile()
    {
        using HaloDesktop::Services::Auth::SessionStore;
        using HaloDesktop::Services::Auth::StoredIdentity;

        TemporaryDirectory const temporary{ L"HaloDesktop-Identity-" };
        auto const identityPath = temporary.Path() / L"auth-identity.bin";
        SessionStore store{ temporary.Path() };

        HaloDesktop::Security::WriteProtectedTextAsync(
            identityPath, R"({"userId":"u1","username":"kenneth"})").get();
        auto const previous = store.LoadIdentityAsync().get();
        Require(previous && previous->UserId == L"u1" && previous->Username == L"kenneth",
            "an identity saved by the previous version did not restore");
        Require(!previous->IsAdmin && !previous->Plan && !previous->AvatarVersion,
            "an identity without a profile restored one");

        store.SaveIdentityAsync(StoredIdentity{
            .UserId = L"u1",
            .Username = L"kenneth",
            .IsAdmin = true,
            .Plan = winrt::hstring{ L"premium" },
            .AvatarVersion = winrt::hstring{ L"v3" },
        }, 1).get();
        auto const saved = store.LoadIdentityAsync().get();
        Require(saved && saved->UserId == L"u1" && saved->IsAdmin.value_or(false)
                && saved->Plan.value_or(L"") == L"premium" && saved->AvatarVersion.value_or(L"") == L"v3",
            "the saved profile did not round-trip");
        auto const bytes = ReadBytes(identityPath);
        std::string const raw{ bytes.begin(), bytes.end() };
        Require(raw.find("kenneth") == std::string::npos && raw.find("premium") == std::string::npos,
            "the saved identity was written unprotected");

        store.SaveIdentityAsync(StoredIdentity{ .UserId = L"u1", .Username = L"kenneth", .IsAdmin = false }, 2).get();
        auto const plain = store.LoadIdentityAsync().get();
        Require(plain && plain->IsAdmin.has_value() && !*plain->IsAdmin && !plain->Plan && !plain->AvatarVersion,
            "a profile without a tier or picture did not round-trip");

        HaloDesktop::Security::WriteProtectedTextAsync(
            identityPath,
            R"({"userId":"u1","username":"kenneth","isAdmin":"yes","plan":5,"avatarVersion":""})").get();
        auto const odd = store.LoadIdentityAsync().get();
        Require(odd && odd->UserId == L"u1", "a malformed profile field failed the restore");
        Require(!odd->IsAdmin && !odd->Plan && !odd->AvatarVersion, "a malformed profile field was kept");
    }
}

void RunStandaloneStorageTests()
{
    TestSavedIdentityProfile();
    TestPreferenceValidationAndConcurrentMerge();
    TestUnreadablePreferencesAreNotReplaced();
    TestSubtitleMemoryFitsTheFile();
    TestSearchHistoryShapes();
    TestMigrationNoSource();
    TestMigrationSuccessRetryAndIdempotency();
    TestMigrationProtectsExistingTarget();
    TestDownloadLeasesAndAtomicIndexMerge();
    TestCorruptDownloadIndexRecovery();
    TestDownloadLandscapeArtworkPersistence();
    TestPendingDownloadDeletionRecovery();
    TestMissingCompletedDownloadRecovery();
    TestDownloadsOutsideApprovedRootsAreQuarantined();
}
