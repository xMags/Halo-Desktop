#include "pch.h"
#include "Services/Downloads/TransferEngine.h"

#include "Services/Downloads/DownloadAdoptionPolicy.h"
#include "Services/Downloads/DownloadHttp.h"
#include "Services/Downloads/SegmentPlanner.h"
#include "Storage/FileStorage.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <wil/resource.h>

namespace
{
    constexpr std::size_t TransferChunkSize = 64u * 1024u;
    constexpr std::uint64_t MaximumSubtitleBytes = 32ull * 1024ull * 1024ull;
    constexpr int TransferAttempts = 3;
    // How often, and after how long a growing wait, the end of a finished
    // download is stored again when the index cannot be written.
    constexpr int FinishPersistAttempts = 3;
    constexpr std::chrono::milliseconds FinishPersistRetry{ 200 };
    constexpr char const* DeleteFailed =
        "Halo could not delete every file for this download. It will try again the next time Halo starts.";

    std::uint64_t NowMilliseconds() noexcept
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    std::wstring NewJobId()
    {
        GUID id{};
        winrt::check_hresult(CoCreateGuid(&id));
        std::array<wchar_t, 40> value{};
        if (StringFromGUID2(id, value.data(), static_cast<int>(value.size())) == 0)
        {
            throw std::runtime_error{ "A download job identifier could not be created." };
        }
        std::wstring result;
        result.reserve(32);
        for (auto const character : value)
        {
            if ((character >= L'0' && character <= L'9')
                || (character >= L'a' && character <= L'f')
                || (character >= L'A' && character <= L'F'))
            {
                result.push_back(static_cast<wchar_t>(std::towlower(character)));
            }
        }
        if (result.size() != 32)
        {
            throw std::runtime_error{ "A download job identifier is invalid." };
        }
        return result;
    }

    std::optional<std::uint64_t> DiskFree(std::filesystem::path const& path) noexcept
    {
        ULARGE_INTEGER available{};
        if (!GetDiskFreeSpaceExW(path.c_str(), &available, nullptr, nullptr))
        {
            return std::nullopt;
        }
        return available.QuadPart;
    }

    void RemoveFileIfPresent(std::filesystem::path const& path)
    {
        std::error_code error;
        auto const removed = std::filesystem::remove(path, error);
        if (error)
        {
            throw std::system_error{ error, "Could not remove a download file" };
        }
        if (!removed && std::filesystem::exists(path, error))
        {
            throw std::runtime_error{ "A download file could not be removed." };
        }
        if (error)
        {
            throw std::system_error{ error, "Could not verify download file removal" };
        }
    }

    std::optional<bool> PathsMatch(
        std::filesystem::path const& left,
        std::filesystem::path const& right) noexcept
    {
        try
        {
            auto const normalizedLeft = std::filesystem::absolute(left).lexically_normal().wstring();
            auto const normalizedRight = std::filesystem::absolute(right).lexically_normal().wstring();
            if (normalizedLeft.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())
                || normalizedRight.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            {
                return std::nullopt;
            }
            auto const comparison = CompareStringOrdinal(
                normalizedLeft.c_str(),
                static_cast<int>(normalizedLeft.size()),
                normalizedRight.c_str(),
                static_cast<int>(normalizedRight.size()),
                TRUE);
            if (comparison == 0)
            {
                return std::nullopt;
            }
            return comparison == CSTR_EQUAL;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool RecordMayOwnPath(
        HaloDesktop::Services::Downloads::DownloadRecord const& record,
        std::filesystem::path const& path)
    {
        auto matchesPath = [&path](std::filesystem::path const& candidate)
        {
            auto const matches = PathsMatch(candidate, path);
            if (!matches)
            {
                throw std::runtime_error{ "A download path could not be compared safely." };
            }
            return *matches;
        };
        namespace Segments = HaloDesktop::Services::Downloads::Segments;
        auto const table = Segments::TablePath(record.PartialPath());
        if (matchesPath(record.TargetPath())
            || matchesPath(record.PartialPath())
            || matchesPath(table)
            || matchesPath(Segments::TableTempPath(table)))
        {
            return true;
        }
        for (auto const kind : { HaloDesktop::Services::Downloads::SidecarKind::Logo, HaloDesktop::Services::Downloads::SidecarKind::Art })
        {
            if (matchesPath(record.SidecarPath(kind)) || matchesPath(record.SidecarTempPath(kind)))
            {
                return true;
            }
        }
        return record.SubtitleFileName
            && matchesPath(record.RootPath / *record.SubtitleFileName);
    }

    // Raised instead of deleting when a record names a file outside every folder
    // this device has used for downloads. The record is forgotten rather than
    // acted on, so a corrupt or edited index cannot aim deletion at the rest of
    // the disk, and a failed check does not come back on the next launch.
    struct ContainmentError final
    {
    };

    // The path Windows actually resolves to, with junctions and symbolic links
    // followed. Returns nullopt when the object cannot be opened, which includes
    // the ordinary case of a file that is not there.
    std::optional<std::filesystem::path> RealPath(std::filesystem::path const& value) noexcept
    {
        try
        {
            wil::unique_hfile handle{ CreateFileW(
                value.c_str(),
                0,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS,
                nullptr) };
            if (!handle)
            {
                return std::nullopt;
            }
            std::wstring buffer(MAX_PATH, L'\0');
            auto length = GetFinalPathNameByHandleW(
                handle.get(),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (length == 0)
            {
                return std::nullopt;
            }
            if (length > buffer.size())
            {
                buffer.resize(length);
                length = GetFinalPathNameByHandleW(
                    handle.get(),
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
                if (length == 0 || length > buffer.size())
                {
                    return std::nullopt;
                }
            }
            buffer.resize(length);
            constexpr std::wstring_view uncPrefix = L"\\\\?\\UNC\\";
            constexpr std::wstring_view longPrefix = L"\\\\?\\";
            if (buffer.starts_with(uncPrefix))
            {
                buffer = L"\\\\" + buffer.substr(uncPrefix.size());
            }
            else if (buffer.starts_with(longPrefix))
            {
                buffer.erase(0, longPrefix.size());
            }
            return std::filesystem::path{ buffer };
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::vector<std::filesystem::path> ResolveRoots(
        std::vector<std::filesystem::path> const& approvedRoots)
    {
        // A folder the user picked may itself be a junction. Keeping both the
        // literal and the resolved form means such a choice still matches, while
        // a link planted underneath a root resolves out of the set.
        std::vector<std::filesystem::path> resolved;
        resolved.reserve(approvedRoots.size() * 2);
        for (auto const& root : approvedRoots)
        {
            resolved.push_back(root);
            if (auto real = RealPath(root))
            {
                resolved.push_back(std::move(*real));
            }
        }
        return resolved;
    }

    void EnsureWithinRoots(
        std::filesystem::path const& path,
        std::vector<std::filesystem::path> const& resolvedRoots)
    {
        using HaloDesktop::Services::Downloads::IsWithinApprovedRoot;
        if (!IsWithinApprovedRoot(path, resolvedRoots))
        {
            throw ContainmentError{};
        }
        std::error_code error;
        if (std::filesystem::is_symlink(path, error))
        {
            throw ContainmentError{};
        }
        auto effective = RealPath(path);
        if (!effective)
        {
            // The file is usually already gone. Judge it by the folder that
            // would hold it, so a linked parent is still caught.
            if (auto const parent = RealPath(path.parent_path()))
            {
                effective = *parent / path.filename();
            }
        }
        if (effective && !IsWithinApprovedRoot(*effective, resolvedRoots))
        {
            throw ContainmentError{};
        }
    }

    // A title's own folder goes once its last file has: only when it is empty,
    // is inside a root without being one, and is a real folder rather than a
    // link (removing a link would act on wherever it points). Best effort: a
    // folder the viewer has put something else in is simply kept.
    void RemoveFolderIfLeftEmpty(
        std::filesystem::path const& folder,
        std::vector<std::filesystem::path> const& resolvedRoots) noexcept
    {
        try
        {
            using HaloDesktop::Services::Downloads::IsWithinApprovedRoot;
            if (folder.empty() || !IsWithinApprovedRoot(folder, resolvedRoots))
            {
                return;
            }
            for (auto const& root : resolvedRoots)
            {
                auto const same = PathsMatch(folder, root);
                if (!same || *same)
                {
                    return;
                }
            }
            std::error_code error;
            if (!std::filesystem::is_directory(folder, error) || error
                || std::filesystem::is_symlink(folder, error) || error)
            {
                return;
            }
            auto const real = RealPath(folder);
            auto const unchanged = real ? PathsMatch(*real, folder) : std::nullopt;
            if (!unchanged || !*unchanged)
            {
                return;
            }
            // Refused by the file system unless the folder is empty.
            std::filesystem::remove(folder, error);
        }
        catch (...)
        {
        }
    }

    void RemoveRecordFiles(
        HaloDesktop::Services::Downloads::DownloadRecord const& record,
        std::vector<std::filesystem::path> const& approvedRoots,
        HaloDesktop::Services::Downloads::DownloadRecord const* preservedOwner = nullptr)
    {
        auto const resolvedRoots = ResolveRoots(approvedRoots);
        auto removeUnlessPreserved = [preservedOwner, &resolvedRoots](std::filesystem::path const& path)
        {
            if (!preservedOwner || !RecordMayOwnPath(*preservedOwner, path))
            {
                EnsureWithinRoots(path, resolvedRoots);
                RemoveFileIfPresent(path);
            }
        };
        namespace Segments = HaloDesktop::Services::Downloads::Segments;
        auto const table = Segments::TablePath(record.PartialPath());
        // The part goes before its table: a full-length part must never be left
        // without the table that says which of its bytes arrived.
        removeUnlessPreserved(record.PartialPath());
        removeUnlessPreserved(table);
        removeUnlessPreserved(Segments::TableTempPath(table));
        if (record.SubtitleFileName)
        {
            removeUnlessPreserved(record.RootPath / *record.SubtitleFileName);
        }
        for (auto const kind : { HaloDesktop::Services::Downloads::SidecarKind::Logo, HaloDesktop::Services::Downloads::SidecarKind::Art })
        {
            removeUnlessPreserved(record.SidecarTempPath(kind));
            removeUnlessPreserved(record.SidecarPath(kind));
        }
        // Delete the playable file last. If an auxiliary cleanup fails, the
        // indexed download still has its primary content and can be retried.
        removeUnlessPreserved(record.TargetPath());
        RemoveFolderIfLeftEmpty(record.RootPath, resolvedRoots);
    }

    // True when any file `record` names is also named by another record, which
    // then still needs it: a replacement and the copy it replaces can share a
    // name when both came from the same source.
    // `ignored` names one more record that does not count.
    bool SharesFilesWithAnotherRecord(
        HaloDesktop::Services::Downloads::DownloadRecord const& record,
        std::map<std::wstring, HaloDesktop::Services::Downloads::DownloadRecord, std::less<>> const& records,
        std::wstring_view ignored = {})
    {
        namespace Segments = HaloDesktop::Services::Downloads::Segments;
        auto const table = Segments::TablePath(record.PartialPath());
        std::vector<std::filesystem::path> paths{
            record.PartialPath(),
            table,
            Segments::TableTempPath(table),
            record.TargetPath(),
            record.SidecarPath(HaloDesktop::Services::Downloads::SidecarKind::Logo),
            record.SidecarPath(HaloDesktop::Services::Downloads::SidecarKind::Art),
        };
        if (record.SubtitleFileName)
        {
            paths.push_back(record.RootPath / *record.SubtitleFileName);
        }
        for (auto const& [jobId, other] : records)
        {
            if (jobId == record.JobId || (!ignored.empty() && jobId == ignored))
            {
                continue;
            }
            for (auto const& path : paths)
            {
                if (RecordMayOwnPath(other, path))
                {
                    return true;
                }
            }
        }
        return false;
    }

    // True when a run of `record` renamed its complete file into place and
    // stopped before the end was stored (the index could not be written, or a
    // pause or exit came during the subtitle). No part or table is left, and
    // the file is exactly the size the source stated. An unknown size is never
    // taken on trust: that download runs again.
    bool FinishedFileInPlace(HaloDesktop::Services::Downloads::DownloadRecord const& record) noexcept
    {
        try
        {
            if (record.TotalBytes == 0)
            {
                return false;
            }
            namespace Segments = HaloDesktop::Services::Downloads::Segments;
            std::error_code error;
            auto const part = record.PartialPath();
            auto const table = Segments::TablePath(part);
            if (std::filesystem::exists(part, error) || error
                || std::filesystem::exists(table, error) || error)
            {
                return false;
            }
            auto const target = record.TargetPath();
            if (!std::filesystem::is_regular_file(target, error) || error)
            {
                return false;
            }
            auto const size = std::filesystem::file_size(target, error);
            return !error && size == record.TotalBytes;
        }
        catch (...)
        {
            return false;
        }
    }

    std::wstring SubtitleExtension(std::wstring const& url)
    {
        auto end = url.find_first_of(L"?#");
        auto path = std::filesystem::path{ url.substr(0, end) };
        auto extension = path.extension().wstring();
        if (!extension.empty() && extension.front() == L'.')
        {
            extension.erase(extension.begin());
        }
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        if (extension != L"srt" && extension != L"vtt"
            && extension != L"ass" && extension != L"ssa")
        {
            return L"srt";
        }
        return extension;
    }

    std::wstring SafeLanguage(std::wstring language)
    {
        std::replace_if(language.begin(), language.end(), [](wchar_t character)
        {
            return !(character >= L'a' && character <= L'z')
                && !(character >= L'A' && character <= L'Z')
                && !(character >= L'0' && character <= L'9')
                && character != L'-'
                && character != L'_';
        }, L'_');
        if (language.empty()) language = L"und";
        if (language.size() > 24) language.resize(24);
        return language;
    }
}

namespace HaloDesktop::Services::Downloads
{
    struct TransferEngine::TransferResult final
    {
        std::uint64_t Bytes{};
        std::uint64_t Total{};
        std::optional<std::wstring> Validator;
        std::optional<std::pair<std::wstring, std::wstring>> Subtitle;
    };

    struct TransferEngine::TransferError final
    {
        enum class Kind
        {
            Permanent,
            Retryable,
            Paused,
            Shutdown,
        };

        Kind Type{ Kind::Permanent };
        DownloadFailureCode Failure{ DownloadFailureCode::Unknown };
    };

    TransferEngine::TransferEngine(
        std::filesystem::path dataRoot,
        LaunchRecoveryReporter const& launchRecovery,
        TransferEngineOptions options)
        : m_options(std::move(options)),
          m_store(std::move(dataRoot)),
          m_vault(m_store.Paths().VaultDirectory)
    {
        for (auto& record : m_store.Load())
        {
            m_records.emplace(record.JobId, std::move(record));
        }
        if (m_store.TakeLoadRecoveryNotice() && launchRecovery)
        {
            try
            {
                launchRecovery(
                    L"The download index could not be read at launch. It was kept aside and "
                    + std::to_wstring(m_records.size()) + L" downloads were recovered from it.");
            }
            catch (...)
            {
            }
        }
        std::vector<std::wstring> pendingDeletionIds;
        for (auto const& [jobId, record] : m_records)
        {
            if (record.PendingDeletion) pendingDeletionIds.push_back(jobId);
        }
        auto const approvedRoots = m_store.ApprovedRoots();
        for (auto const& jobId : pendingDeletionIds)
        {
            try
            {
                auto const pending = m_records.at(jobId);
                auto owner = std::find_if(m_records.begin(), m_records.end(), [&jobId](auto const& pair)
                {
                    return pair.second.Replacement && pair.second.Replacement->JobId == jobId;
                });
                auto updatedOwner = owner == m_records.end()
                    ? std::optional<DownloadRecord>{}
                    : std::optional<DownloadRecord>{ owner->second };
                if (updatedOwner)
                {
                    // A pending backup is written only after the replacement file
                    // has completed. Recover that finalization without downloading
                    // the already-finished replacement a second time.
                    updatedOwner->Status = DownloadStatus::Done;
                    updatedOwner->ExplicitPause = false;
                    updatedOwner->BytesPerSecond = 0;
                    updatedOwner->Replacement.reset();
                }
                RemoveRecordFiles(pending, approvedRoots, updatedOwner ? &*updatedOwner : nullptr);
                m_vault.Remove(jobId);
                m_store.Apply(updatedOwner ? std::vector<DownloadRecord>{ *updatedOwner }
                                           : std::vector<DownloadRecord>{},
                    { jobId });
                if (updatedOwner) owner->second = std::move(*updatedOwner);
                m_records.erase(jobId);
            }
            catch (ContainmentError const&)
            {
                // Forget the record and leave its files alone. Retrying this on
                // every launch would never succeed, and deleting is exactly what
                // must not happen outside the download folders.
                OutputDebugStringW(
                    L"Halo forgot a deletion that pointed outside its download folders.\n");
                try { m_vault.Remove(jobId); } catch (...) {}
                try { m_store.Apply({}, { jobId }); } catch (...) {}
                m_records.erase(jobId);
            }
            catch (...)
            {
            }
        }
        m_worker = std::jthread{ [this](std::stop_token stopToken)
        {
            Worker(stopToken);
        } };
    }

    TransferEngine::~TransferEngine()
    {
        m_worker.request_stop();
        {
            std::scoped_lock const lock{ m_mutex };
            for (auto const& [jobId, flag] : m_cancel)
            {
                static_cast<void>(jobId);
                flag->store(true, std::memory_order_release);
            }
        }
        m_condition.notify_all();
        if (m_worker.joinable())
        {
            m_worker.join();
        }
    }

    void TransferEngine::SetAccount(std::wstring serverUrl, std::wstring userId)
    {
        auto const key = MakeAccountKey(std::move(serverUrl), userId);
        std::vector<std::wstring> pauseIds;
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_activeAccount && *m_activeAccount != key)
            {
                for (auto const& [jobId, record] : m_records)
                {
                    if (record.AccountKey == *m_activeAccount && IsActive(record.Status))
                    {
                        pauseIds.push_back(jobId);
                    }
                }
            }
        }
        std::exception_ptr pauseFailure;
        for (auto const& jobId : pauseIds)
        {
            try
            {
                // Not the viewer's pause: an adoption by the next owner
                // carries the transfer on as it was.
                PauseFor(jobId, false);
            }
            catch (...)
            {
                if (!pauseFailure) pauseFailure = std::current_exception();
            }
        }

        auto latest = m_store.Load(false);
        {
            std::scoped_lock const lock{ m_mutex };
            std::optional<DownloadRecord> activeRecord;
            if (m_activeJob)
            {
                if (auto const active = m_records.find(*m_activeJob); active != m_records.end())
                {
                    activeRecord = active->second;
                }
            }
            m_records.clear();
            for (auto& record : latest)
            {
                m_records.emplace(record.JobId, std::move(record));
            }
            if (activeRecord)
            {
                // The job lease prevents another process from changing this row
                // while our worker owns it. Keep the live progress state instead
                // of downgrading the durable "downloading" marker to queued.
                m_records.insert_or_assign(activeRecord->JobId, std::move(*activeRecord));
            }
            m_activeAccount = key;
            m_queue.clear();
            std::vector<std::reference_wrapper<DownloadRecord const>> queued;
            for (auto& [jobId, record] : m_records)
            {
                static_cast<void>(jobId);
                if (record.AccountKey == key
                    && (!m_activeJob || record.JobId != *m_activeJob)
                    && record.Status == DownloadStatus::Downloading)
                {
                    record.Status = DownloadStatus::Queued;
                    record.BytesPerSecond = 0;
                }
                if (record.AccountKey == key
                    && record.Status == DownloadStatus::Queued
                    && !record.ExplicitPause
                    && !record.PendingDeletion
                    && !IsHiddenBackupLocked(record.JobId))
                {
                    queued.emplace_back(record);
                }
            }
            std::sort(queued.begin(), queued.end(), [](auto const& left, auto const& right)
            {
                auto const& a = left.get();
                auto const& b = right.get();
                return std::tie(a.CreatedAt, a.JobId) < std::tie(b.CreatedAt, b.JobId);
            });
            for (auto const& record : queued)
            {
                m_queue.push_back(record.get().JobId);
            }
        }
        m_condition.notify_all();
        if (pauseFailure)
        {
            OutputDebugStringW(L"Halo could not pause every old-account download.\n");
        }
    }

    void TransferEngine::ClearAccount()
    {
        std::vector<std::wstring> pauseIds;
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount)
            {
                return;
            }
            for (auto const& [jobId, record] : m_records)
            {
                if (record.AccountKey == *m_activeAccount && IsActive(record.Status))
                {
                    pauseIds.push_back(jobId);
                }
            }
        }
        std::exception_ptr pauseFailure;
        for (auto const& jobId : pauseIds)
        {
            try
            {
                PauseFor(jobId, false);
            }
            catch (...)
            {
                if (!pauseFailure) pauseFailure = std::current_exception();
            }
        }
        {
            std::scoped_lock const lock{ m_mutex };
            m_activeAccount.reset();
            m_queue.clear();
        }
        if (pauseFailure)
        {
            OutputDebugStringW(L"Halo could not pause every signed-out download.\n");
        }
    }

    std::vector<DownloadRecord> TransferEngine::List()
    {
        ReconcileMissingFiles();
        std::scoped_lock const lock{ m_mutex };
        if (!m_activeAccount)
        {
            return {};
        }
        std::vector<DownloadRecord> result;
        for (auto const& [jobId, record] : m_records)
        {
            if (record.AccountKey == *m_activeAccount
                && !record.PendingDeletion
                && !IsHiddenBackupLocked(jobId))
            {
                result.push_back(record);
            }
        }
        std::sort(result.begin(), result.end(), [](DownloadRecord const& left, DownloadRecord const& right)
        {
            return std::tie(left.CreatedAt, left.JobId) < std::tie(right.CreatedAt, right.JobId);
        });
        return result;
    }

    bool TransferEngine::SetLandscapeArtwork(
        std::wstring const& jobId,
        std::wstring const& expectedAccount,
        std::wstring artwork)
    {
        if (artwork.empty())
        {
            return false;
        }
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount || *m_activeAccount != expectedAccount)
            {
                return false;
            }
        }

        auto const persisted = m_store.SetLandscapeArtwork(
            jobId,
            expectedAccount,
            std::move(artwork));
        if (!persisted)
        {
            return false;
        }

        auto changed = *persisted;
        std::vector<DownloadChangedHandler> handlers;
        {
            std::scoped_lock const lock{ m_mutex };
            auto const current = m_records.find(jobId);
            if (current == m_records.end() || current->second.AccountKey != expectedAccount)
            {
                return true;
            }
            current->second.Media.LandscapeArtwork = changed.Media.LandscapeArtwork;
            current->second.UpdatedAt = (std::max)(current->second.UpdatedAt, changed.UpdatedAt);
            changed = current->second;
            handlers = HandlersLocked();
        }
        Notify(handlers, changed);
        return true;
    }

    DownloadRecord TransferEngine::Start(DownloadStartRequest request)
    {
        if (request.Media.VideoId.empty()
            || request.Media.VideoId.size() > 1024
            || request.Media.ItemId.empty()
            || request.Media.Title.empty())
        {
            throw std::invalid_argument{ "Complete media metadata is required for a download." };
        }
        ValidateProtectedRequest(request.Request);
        auto const fingerprint = Sha256Hex(request.Request.Url);

        std::optional<DownloadRecord> existing;
        std::filesystem::path directory;
        std::wstring account;
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount)
            {
                throw std::runtime_error{ "Sign in before downloading." };
            }
            account = *m_activeAccount;
        }
        ::HaloDesktop::Storage::FileMutationLock const preparationLease{
            m_store.Paths().DataRoot / (L"download-start-" + Sha256Hex(account + L"\n" + request.Media.VideoId)) };
        auto latest = m_store.Load(false);
        std::set<std::wstring, std::less<>> latestIds;
        for (auto const& record : latest) latestIds.insert(record.JobId);
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount || *m_activeAccount != account)
            {
                throw std::runtime_error{ "The active account changed while starting the download." };
            }
            for (auto& record : latest)
            {
                if (!m_activeJob || *m_activeJob != record.JobId)
                {
                    m_records.insert_or_assign(record.JobId, std::move(record));
                }
            }
            std::erase_if(m_records, [this, &latestIds](auto const& pair)
            {
                return !latestIds.contains(pair.first)
                    && (!m_activeJob || *m_activeJob != pair.first);
            });
            if (!m_pendingVideoIds.insert(request.Media.VideoId).second)
            {
                throw std::runtime_error{ "This video is already being prepared for download." };
            }
            existing = VisibleRecordForVideoLocked(request.Media.VideoId);
            directory = m_store.DownloadDirectory();
        }
        auto clearPending = wil::scope_exit([this, videoId = request.Media.VideoId]() noexcept
        {
            std::scoped_lock const lock{ m_mutex };
            m_pendingVideoIds.erase(videoId);
        });

        if (existing && existing->SourceFingerprint == fingerprint
            && existing->Status != DownloadStatus::Failed)
        {
            return *existing;
        }
        if (existing && existing->SourceFingerprint != fingerprint && !request.ReplaceExisting)
        {
            throw std::runtime_error{ "A different source is already saved for this video. Confirm replacement first." };
        }
        if (existing && existing->Status == DownloadStatus::Failed
            && existing->Failure && RequiresNewSource(*existing->Failure)
            && !request.ReplaceExisting)
        {
            throw std::runtime_error{ "Choose a source again and confirm replacement before retrying this download." };
        }
        if (existing && existing->Status == DownloadStatus::Failed
            && (!existing->Failure || !RequiresNewSource(*existing->Failure))
            && !request.ReplaceExisting)
        {
            Resume(existing->JobId);
            auto const records = List();
            auto const resumed = std::find_if(records.begin(), records.end(), [&existing](DownloadRecord const& record)
            {
                return record.JobId == existing->JobId;
            });
            if (resumed == records.end())
            {
                throw std::runtime_error{ "Download state changed. Try again." };
            }
            return *resumed;
        }
        std::unique_ptr<::HaloDesktop::Storage::FileMutationLock> replacementLease;
        std::optional<DownloadRecord> replacementRollback;
        bool replacementWasActive{};
        if (existing && request.ReplaceExisting)
        {
            replacementWasActive = IsActive(existing->Status);
            {
                std::scoped_lock const lock{ m_mutex };
                if (auto const flag = m_cancel.find(existing->JobId); flag != m_cancel.end())
                {
                    flag->second->store(true, std::memory_order_release);
                }
            }
            replacementLease = std::make_unique<::HaloDesktop::Storage::FileMutationLock>(
                m_store.Paths().DataRoot / (L"download-job-" + existing->JobId));
            latest = m_store.Load(false);
            auto const current = std::find_if(latest.begin(), latest.end(), [&existing](DownloadRecord const& record)
            {
                return record.JobId == existing->JobId;
            });
            if (current == latest.end())
            {
                throw std::runtime_error{ "The existing download changed while preparing its replacement." };
            }
            existing = *current;
            replacementRollback = existing;
            std::scoped_lock const lock{ m_mutex };
            m_records.insert_or_assign(existing->JobId, *existing);
        }
        if (!std::filesystem::is_directory(directory))
        {
            throw std::runtime_error{ "The download folder is unavailable. Choose another folder to continue." };
        }
        // Each title gets its own folder in the download folder, which holds its
        // video, subtitle and pictures (one per show for episodes). A folder made
        // here and left empty by a start that then fails is taken away again.
        directory /= MakeDownloadFolderName(request.Media);
        bool createdFolder{};
        {
            std::error_code error;
            createdFolder = std::filesystem::create_directory(directory, error);
            if (error || !std::filesystem::is_directory(directory, error))
            {
                throw std::runtime_error{ "A folder for this title could not be made in the download folder." };
            }
        }
        auto removeCreatedFolder = wil::scope_exit([&directory, createdFolder]() noexcept
        {
            if (createdFolder)
            {
                std::error_code ignored;
                std::filesystem::remove(directory, ignored);
            }
        });
        try
        {
            // A link planted under the download folder must not lead the download elsewhere.
            EnsureWithinRoots(directory, ResolveRoots(m_store.ApprovedRoots()));
        }
        catch (ContainmentError const&)
        {
            throw std::runtime_error{ "This title's folder leads outside the download folder. Remove it and try again." };
        }
        if (request.Media.VideoSize)
        {
            auto const free = DiskFree(directory);
            if (free && !HasSufficientSpace(*free, *request.Media.VideoSize))
            {
                throw std::runtime_error{ "There is not enough free space for this video." };
            }
        }

        auto const jobId = NewJobId();
        auto const fileName = MakeDownloadFileName(request.Media, fingerprint, account);
        if (!IsSafeFileName(fileName))
        {
            throw std::runtime_error{ "A safe download filename could not be created." };
        }
        auto const target = directory / fileName;
        // The copy being replaced may name this very file (a replacement from
        // the same source has the same name); it is the one record allowed to.
        auto const replacedJob = existing && request.ReplaceExisting
            ? std::optional<std::wstring>{ existing->JobId }
            : std::nullopt;
        auto partial = target;
        partial += L".part";
        if (std::filesystem::exists(target))
        {
            throw std::runtime_error{ "A file with this download name already exists." };
        }
        if (std::filesystem::exists(partial))
        {
            bool ownedByReplaced{};
            try
            {
                ownedByReplaced = replacedJob && RecordMayOwnPath(*existing, partial);
            }
            catch (...)
            {
                // Not comparable: refused, as an unknown owner would be.
            }
            if (!ownedByReplaced)
            {
                throw std::runtime_error{ "A file with this download name already exists." };
            }
        }

        m_vault.Write(jobId, request.Request);
        auto removeVault = wil::scope_exit([this, &jobId]() noexcept
        {
            try { m_vault.Remove(jobId); } catch (...) {}
        });
        auto const now = NowMilliseconds();
        DownloadRecord record{
            .JobId = jobId,
            .AccountKey = account,
            .Media = std::move(request.Media),
            .FileName = fileName,
            .RootPath = directory,
            .Status = DownloadStatus::Queued,
            .SourceFingerprint = fingerprint,
            .CreatedAt = now,
            .UpdatedAt = now,
        };
        if (existing && request.ReplaceExisting)
        {
            record.Replacement = ReplacementBackup{
                .JobId = existing->JobId,
                .RootPath = existing->RootPath,
                .FileName = existing->FileName,
                .SubtitleFileName = existing->SubtitleFileName,
            };
        }

        std::vector<DownloadRecord> snapshot;
        std::optional<DownloadRecord> previousExisting;
        std::uint64_t generation{};
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount || *m_activeAccount != account)
            {
                throw std::runtime_error{ "The active account changed while starting the download." };
            }
            // Decided here, where the record is added, so two starts of one
            // source under different videos cannot both take the name. Every
            // owner's records count: removing one must never delete another's
            // file.
            bool nameTaken{};
            try
            {
                nameTaken = SharesFilesWithAnotherRecord(
                    record, m_records, replacedJob ? std::wstring_view{ *replacedJob } : std::wstring_view{});
            }
            catch (...)
            {
                nameTaken = true;
            }
            if (nameTaken)
            {
                throw std::runtime_error{ "A file with this download name already exists." };
            }
            if (existing)
            {
                auto const found = m_records.find(existing->JobId);
                if (found == m_records.end()
                    || found->second.SourceFingerprint != existing->SourceFingerprint)
                {
                    throw std::runtime_error{ "Download state changed. Try again." };
                }
                if (request.ReplaceExisting && IsActive(found->second.Status))
                {
                    previousExisting = found->second;
                    found->second.Status = DownloadStatus::Paused;
                    found->second.ExplicitPause = true;
                    found->second.BytesPerSecond = 0;
                    found->second.ActiveConnections = 0;
                    if (auto const flag = m_cancel.find(found->second.JobId); flag != m_cancel.end())
                    {
                        flag->second->store(true, std::memory_order_release);
                    }
                    std::erase(m_queue, found->second.JobId);
                }
            }
            m_records.emplace(record.JobId, record);
            m_queue.push_back(record.JobId);
            generation = ++m_generation;
            snapshot = { record };
            if (previousExisting)
            {
                snapshot.push_back(m_records.at(previousExisting->JobId));
            }
        }
        try
        {
            Persist(std::move(snapshot), generation);
        }
        catch (...)
        {
            std::optional<DownloadRecord> restoredForPersistence;
            {
                std::scoped_lock const lock{ m_mutex };
                m_records.erase(record.JobId);
                std::erase(m_queue, record.JobId);
                auto rollback = replacementRollback
                    ? replacementRollback
                    : previousExisting;
                if (rollback)
                {
                    if (replacementWasActive
                        && (IsActive(rollback->Status)
                            || (rollback->Status == DownloadStatus::Paused
                                && !rollback->ExplicitPause)))
                    {
                        rollback->Status = DownloadStatus::Queued;
                        rollback->ExplicitPause = false;
                        rollback->BytesPerSecond = 0;
                        rollback->UpdatedAt = NowMilliseconds();
                        restoredForPersistence = rollback;
                    }
                    m_records.insert_or_assign(rollback->JobId, *rollback);
                    if (rollback->Status == DownloadStatus::Queued
                        && !rollback->ExplicitPause
                        && std::find(m_queue.begin(), m_queue.end(), rollback->JobId) == m_queue.end())
                    {
                        m_queue.push_front(rollback->JobId);
                    }
                }
            }
            if (restoredForPersistence)
            {
                try { m_store.Apply({ *restoredForPersistence }); } catch (...) {}
                m_condition.notify_all();
            }
            throw;
        }
        removeVault.release();
        removeCreatedFolder.release();
        std::vector<DownloadChangedHandler> handlers;
        {
            std::scoped_lock const lock{ m_mutex };
            handlers = HandlersLocked();
        }
        Notify(handlers, record);
        m_condition.notify_all();
        return record;
    }

    bool TransferEngine::SaveSidecar(
        std::wstring const& jobId,
        std::wstring const& expectedAccount,
        SidecarKind kind,
        std::vector<std::uint8_t> const& bytes)
    {
        if (bytes.empty() || bytes.size() > MaximumSidecarBytes)
        {
            return false;
        }
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount || *m_activeAccount != expectedAccount)
            {
                return false;
            }
        }
        // The job's lease keeps a removal from deleting the folder under the
        // write, and a removal that already ran from having the picture come
        // back after it. Never waited for: a running transfer holds it.
        std::unique_ptr<::HaloDesktop::Storage::FileMutationLock> jobLease;
        try
        {
            jobLease = std::make_unique<::HaloDesktop::Storage::FileMutationLock>(
                m_store.Paths().DataRoot / (L"download-job-" + jobId),
                std::chrono::milliseconds{ 0 });
        }
        catch (...)
        {
            return false;
        }
        auto const latest = m_store.Load(false);
        auto const stored = std::find_if(latest.begin(), latest.end(), [&jobId](DownloadRecord const& record)
        {
            return record.JobId == jobId;
        });
        if (stored == latest.end()
            || stored->AccountKey != expectedAccount
            || stored->PendingDeletion
            || stored->Status != DownloadStatus::Done)
        {
            return false;
        }
        auto const target = stored->SidecarPath(kind);
        auto const temporary = stored->SidecarTempPath(kind);
        try
        {
            auto const roots = ResolveRoots(m_store.ApprovedRoots());
            EnsureWithinRoots(target, roots);
            EnsureWithinRoots(temporary, roots);
        }
        catch (...)
        {
            return false;
        }
        std::error_code error;
        if (!std::filesystem::is_directory(stored->RootPath, error) || error)
        {
            return false;
        }
        try
        {
            {
                wil::unique_hfile file{ CreateFileW(
                    temporary.c_str(),
                    GENERIC_WRITE,
                    0,
                    nullptr,
                    CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr) };
                if (!file)
                {
                    return false;
                }
                DWORD written{};
                if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
                    || written != bytes.size()
                    || !FlushFileBuffers(file.get()))
                {
                    file.reset();
                    std::filesystem::remove(temporary, error);
                    return false;
                }
            }
            if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                std::filesystem::remove(temporary, error);
                return false;
            }
            return true;
        }
        catch (...)
        {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

    void TransferEngine::Pause(std::wstring const& jobId)
    {
        PauseFor(jobId, true);
    }

    void TransferEngine::PauseFor(std::wstring const& jobId, bool explicitPause)
    {
        {
            std::scoped_lock const lock{ m_mutex };
            if (auto const flag = m_cancel.find(jobId); flag != m_cancel.end())
            {
                flag->second->store(true, std::memory_order_release);
            }
            else if (auto const live = m_records.find(jobId);
                live != m_records.end()
                && m_activeAccount
                && live->second.AccountKey == *m_activeAccount
                && IsActive(live->second.Status)
                && !live->second.PendingDeletion)
            {
                // In memory first, as Remove does: a worker that already took
                // the job's lease but has not published its cancel flag keeps
                // this pause when it refreshes the record, instead of starting
                // a transfer this call would then wait out.
                live->second.Status = DownloadStatus::Paused;
                live->second.ExplicitPause = explicitPause;
                live->second.BytesPerSecond = 0;
                std::erase(m_queue, jobId);
            }
        }
        ::HaloDesktop::Storage::FileMutationLock const jobLease{
            m_store.Paths().DataRoot / (L"download-job-" + jobId) };
        auto latest = m_store.Load(false);
        auto const persisted = std::find_if(latest.begin(), latest.end(), [&jobId](DownloadRecord const& record)
        {
            return record.JobId == jobId;
        });
        if (persisted == latest.end())
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.erase(jobId);
            std::erase(m_queue, jobId);
            return;
        }
        auto changed = *persisted;
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount || changed.AccountKey != *m_activeAccount)
            {
                throw std::runtime_error{ "This download belongs to another account." };
            }
        }
        if (changed.Status == DownloadStatus::Done || changed.PendingDeletion)
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.insert_or_assign(jobId, changed);
            std::erase(m_queue, jobId);
            return;
        }
        // A pause the viewer already made stays theirs when the owner changes.
        changed.ExplicitPause = explicitPause
            || (changed.Status == DownloadStatus::Paused && changed.ExplicitPause);
        changed.Status = DownloadStatus::Paused;
        changed.BytesPerSecond = 0;
        changed.UpdatedAt = NowMilliseconds();
        m_store.Apply({ changed });
        std::vector<DownloadChangedHandler> handlers;
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.insert_or_assign(jobId, changed);
            std::erase(m_queue, jobId);
            handlers = HandlersLocked();
        }
        Notify(handlers, changed);
    }

    void TransferEngine::Resume(std::wstring const& jobId)
    {
        ::HaloDesktop::Storage::FileMutationLock const jobLease{
            m_store.Paths().DataRoot / (L"download-job-" + jobId) };
        auto latest = m_store.Load(false);
        auto const persisted = std::find_if(latest.begin(), latest.end(), [&jobId](DownloadRecord const& record)
        {
            return record.JobId == jobId;
        });
        if (persisted == latest.end())
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.erase(jobId);
            std::erase(m_queue, jobId);
            return;
        }
        auto changed = *persisted;
        if (changed.Failure && RequiresNewSource(*changed.Failure))
        {
            throw std::runtime_error{ "Choose the source again before retrying this download." };
        }
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount || changed.AccountKey != *m_activeAccount)
            {
                throw std::runtime_error{ "This download belongs to another account." };
            }
        }
        if (changed.Status == DownloadStatus::Done || changed.PendingDeletion)
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.insert_or_assign(jobId, changed);
            std::erase(m_queue, jobId);
            return;
        }
        changed.Status = DownloadStatus::Queued;
        changed.ExplicitPause = false;
        changed.Failure.reset();
        changed.BytesPerSecond = 0;
        changed.UpdatedAt = NowMilliseconds();
        m_store.Apply({ changed });
        std::vector<DownloadChangedHandler> handlers;
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.insert_or_assign(jobId, changed);
            if (std::find(m_queue.begin(), m_queue.end(), jobId) == m_queue.end())
            {
                m_queue.push_back(jobId);
            }
            handlers = HandlersLocked();
        }
        Notify(handlers, changed);
        m_condition.notify_all();
    }

    void TransferEngine::Remove(std::wstring const& jobId)
    {
        static_cast<void>(RemoveOwned(jobId, nullptr));
    }

    bool TransferEngine::Cancel(std::wstring const& jobId)
    {
        return RemoveOwned(jobId, nullptr, RemovalScope::UnfinishedOnly);
    }

    void TransferEngine::AdoptOtherOwners()
    {
        std::wstring active;
        {
            std::scoped_lock const lock{ m_mutex };
            if (!m_activeAccount)
            {
                return;
            }
            active = *m_activeAccount;
        }
        for (auto const& owner : OtherOwners(m_store.Load(false), active))
        {
            AdoptAccount(owner, active);
        }
    }

    void TransferEngine::AdoptAccount(std::wstring const& from, std::wstring const& to)
    {
        if (from.empty() || to.empty() || from == to)
        {
            return;
        }
        auto const plan = PlanAdoption(m_store.Load(false), from, to);

        // The owner change first, as the reference does: until the duplicates
        // are settled nothing is deleted, and a stop in between leaves both
        // copies rather than none.
        auto const adopted = m_store.AdoptAccount(from, to, plan.Rekey, NowMilliseconds());

        // The owner change that came first (SetAccount) paused the previous
        // owner's transfers without the viewer asking. Those carry on under the
        // new owner; a pause the viewer made stays. Stored before memory moves,
        // so the worker's refresh from the index does not pause them again; if
        // it cannot be stored they stay paused and can be resumed by hand.
        std::vector<DownloadRecord> carriedOn;
        {
            std::scoped_lock const lock{ m_mutex };
            if (m_activeAccount && *m_activeAccount == to)
            {
                for (auto const& record : adopted)
                {
                    auto const live = m_records.find(record.JobId);
                    auto resumed = live != m_records.end() ? live->second : record;
                    if (resumed.Status != DownloadStatus::Paused
                        || resumed.ExplicitPause
                        || resumed.PendingDeletion
                        || (m_activeJob && *m_activeJob == record.JobId)
                        || IsHiddenBackupLocked(record.JobId))
                    {
                        continue;
                    }
                    resumed.AccountKey = record.AccountKey;
                    resumed.Status = DownloadStatus::Queued;
                    resumed.BytesPerSecond = 0;
                    resumed.UpdatedAt = (std::max)(resumed.UpdatedAt, NowMilliseconds());
                    carriedOn.push_back(std::move(resumed));
                }
            }
        }
        if (!carriedOn.empty())
        {
            try
            {
                m_store.Apply(carriedOn);
            }
            catch (...)
            {
                carriedOn.clear();
            }
        }

        std::vector<DownloadChangedHandler> handlers;
        std::vector<DownloadRecord> changed;
        {
            std::scoped_lock const lock{ m_mutex };
            for (auto const& record : adopted)
            {
                auto const live = m_records.find(record.JobId);
                if (live == m_records.end())
                {
                    m_records.emplace(record.JobId, record);
                }
                else
                {
                    live->second.AccountKey = record.AccountKey;
                    live->second.UpdatedAt = (std::max)(live->second.UpdatedAt, record.UpdatedAt);
                }
                changed.push_back(m_records.at(record.JobId));
            }
            for (auto const& resumed : carriedOn)
            {
                auto const live = m_records.find(resumed.JobId);
                if (live != m_records.end()
                    && live->second.Status == DownloadStatus::Paused
                    && !live->second.ExplicitPause
                    && !live->second.PendingDeletion)
                {
                    live->second.Status = DownloadStatus::Queued;
                    live->second.BytesPerSecond = 0;
                    live->second.UpdatedAt = resumed.UpdatedAt;
                    auto const listed = std::find_if(changed.begin(), changed.end(), [&resumed](DownloadRecord const& record)
                    {
                        return record.JobId == resumed.JobId;
                    });
                    if (listed != changed.end())
                    {
                        *listed = live->second;
                    }
                }
            }
            if (m_activeAccount && *m_activeAccount == to)
            {
                std::vector<std::reference_wrapper<DownloadRecord const>> queued;
                for (auto const& record : changed)
                {
                    auto const& live = m_records.at(record.JobId);
                    if (live.Status == DownloadStatus::Queued
                        && !live.ExplicitPause
                        && !live.PendingDeletion
                        && !IsHiddenBackupLocked(live.JobId)
                        && std::find(m_queue.begin(), m_queue.end(), live.JobId) == m_queue.end())
                    {
                        queued.emplace_back(live);
                    }
                }
                std::sort(queued.begin(), queued.end(), [](auto const& left, auto const& right)
                {
                    auto const& a = left.get();
                    auto const& b = right.get();
                    return std::tie(a.CreatedAt, a.JobId) < std::tie(b.CreatedAt, b.JobId);
                });
                for (auto const& record : queued)
                {
                    m_queue.push_back(record.get().JobId);
                }
            }
            handlers = HandlersLocked();
        }
        for (auto const& record : changed)
        {
            Notify(handlers, record);
        }
        m_condition.notify_all();

        // A replacing record before the backup it names: the backup cannot be
        // removed while a record still points at it.
        auto discard = plan.Discard;
        {
            auto const latest = m_store.Load(false);
            std::stable_partition(discard.begin(), discard.end(), [&latest](std::wstring const& jobId)
            {
                return std::any_of(latest.begin(), latest.end(), [&jobId](DownloadRecord const& record)
                {
                    return record.JobId == jobId && record.Replacement.has_value();
                });
            });
        }
        std::vector<std::wstring> const owners{ from, to };
        for (auto const& jobId : discard)
        {
            try
            {
                RemoveOwned(jobId, &owners);
            }
            catch (...)
            {
                // A file that cannot be deleted now keeps its tombstone and is
                // removed at the next launch; the adoption itself holds.
            }
        }
    }

    bool TransferEngine::RemoveOwned(
        std::wstring const& jobId,
        std::vector<std::wstring> const* owners,
        RemovalScope scope)
    {
        std::unique_lock gate{ m_removalGate };
        auto latest = m_store.Load(false);
        // A replacement that is itself being removed will never finish, so the
        // copy it named is free again (as IsHiddenBackupLocked already shows it).
        if (std::any_of(latest.begin(), latest.end(), [&jobId](DownloadRecord const& record)
            {
                return record.Replacement
                    && record.Replacement->JobId == jobId
                    && !record.PendingDeletion;
            }))
        {
            throw std::runtime_error{ "The previous file is retained until its replacement finishes." };
        }
        auto const persisted = std::find_if(latest.begin(), latest.end(), [&jobId](DownloadRecord const& record)
        {
            return record.JobId == jobId;
        });
        DownloadRecord removed;
        {
            std::scoped_lock const lock{ m_mutex };
            auto const active = m_activeJob && *m_activeJob == jobId;
            auto const live = m_records.find(jobId);
            if (persisted == latest.end() && (!active || live == m_records.end()))
            {
                m_records.erase(jobId);
                std::erase(m_queue, jobId);
                return true;
            }
            // The worker's copy carries the live progress; the stored one is
            // what everyone else last saw.
            removed = active && live != m_records.end() ? live->second : *persisted;
            auto const owned = owners
                ? std::find(owners->begin(), owners->end(), removed.AccountKey) != owners->end()
                : m_activeAccount && removed.AccountKey == *m_activeAccount;
            if (!owned)
            {
                throw std::runtime_error{ "This download belongs to another account." };
            }
            // The end of a run stores Done under this same gate, so either copy
            // saying Done means the transfer finished before the cancel.
            if (scope == RemovalScope::UnfinishedOnly
                && (removed.Status == DownloadStatus::Done
                    || (persisted != latest.end() && persisted->Status == DownloadStatus::Done)
                    || (live != m_records.end() && live->second.Status == DownloadStatus::Done)))
            {
                return false;
            }
            removed.PendingDeletion = true;
            removed.Status = DownloadStatus::Paused;
            removed.ExplicitPause = true;
            removed.BytesPerSecond = 0;
            removed.ActiveConnections = 0;
            removed.UpdatedAt = NowMilliseconds();
            // In memory first, so a worker that picks the job up from here on
            // keeps the tombstone instead of starting it.
            m_records.insert_or_assign(jobId, removed);
            std::erase(m_queue, jobId);
        }
        // Stored before the worker is asked to stop, not after: a transfer in its
        // last moments still finishes, and the end of its run must find the
        // tombstone by then, or it would supersede the copy it was replacing.
        m_store.Apply({ removed });
        bool workerOwnsCleanup{};
        {
            std::scoped_lock const lock{ m_mutex };
            workerOwnsCleanup = m_activeJob && *m_activeJob == jobId;
            if (workerOwnsCleanup)
            {
                if (auto const flag = m_cancel.find(jobId); flag != m_cancel.end())
                {
                    flag->second->store(true, std::memory_order_release);
                }
            }
        }
        if (workerOwnsCleanup)
        {
            // The run deletes its own files once it has stopped writing them.
            m_condition.notify_all();
            return true;
        }
        // No worker here runs it, and none will start it now. Another process's
        // worker may, so its lease is waited for before anything is deleted.
        gate.unlock();
        ::HaloDesktop::Storage::FileMutationLock const jobLease{
            m_store.Paths().DataRoot / (L"download-job-" + jobId) };
        latest = m_store.Load(false);
        auto const current = std::find_if(latest.begin(), latest.end(), [&jobId](DownloadRecord const& record)
        {
            return record.JobId == jobId;
        });
        if (current == latest.end())
        {
            std::scoped_lock const lock{ m_mutex };
            m_records.erase(jobId);
            return true;
        }
        removed = *current;
        if (!removed.PendingDeletion)
        {
            removed.PendingDeletion = true;
            removed.Status = DownloadStatus::Paused;
            removed.ExplicitPause = true;
            removed.BytesPerSecond = 0;
            removed.UpdatedAt = NowMilliseconds();
            m_store.Apply({ removed });
        }
        // As at the end of a removed run: a file another record still names
        // (a replacement and the copy it replaces can share a name) stays. A
        // path that cannot be compared counts as shared, since keeping a file
        // is recoverable and deleting another record's is not.
        bool shared{};
        try
        {
            std::map<std::wstring, DownloadRecord, std::less<>> stored;
            for (auto const& record : latest)
            {
                stored.insert_or_assign(record.JobId, record);
            }
            shared = SharesFilesWithAnotherRecord(removed, stored);
            if (!shared)
            {
                std::scoped_lock const lock{ m_mutex };
                shared = SharesFilesWithAnotherRecord(removed, m_records);
            }
        }
        catch (...)
        {
            shared = true;
        }
        try
        {
            m_vault.Remove(jobId);
            if (!shared)
            {
                RemoveRecordFiles(removed, m_store.ApprovedRoots());
            }
            m_store.Apply({}, { jobId });
            std::scoped_lock const lock{ m_mutex };
            m_records.erase(jobId);
        }
        catch (ContainmentError const&)
        {
            try { m_store.Apply({}, { jobId }); } catch (...) {}
            {
                std::scoped_lock const lock{ m_mutex };
                m_records.erase(jobId);
            }
            throw std::runtime_error{
                "This download is saved outside Halo's download folders. It was removed from the list and its files were left in place." };
        }
        catch (std::exception const&)
        {
            // The tombstone stays, so the next launch finishes the deletion.
            throw std::runtime_error{ DeleteFailed };
        }
        return true;
    }

    PlaybackFiles TransferEngine::FilesForPlayback(std::wstring const& jobId)
    {
        ReconcileMissingFiles();
        auto const resolvedRoots = ResolveRoots(m_store.ApprovedRoots());
        std::scoped_lock const lock{ m_mutex };
        auto const found = m_records.find(jobId);
        if (found == m_records.end()
            || !m_activeAccount
            || found->second.AccountKey != *m_activeAccount
            || found->second.Status != DownloadStatus::Done
            || found->second.PendingDeletion
            // The index names the file to open, so the same containment rule
            // that guards deletion has to decide what the player may load.
            || !IsWithinApprovedRoot(found->second.TargetPath(), resolvedRoots)
            || !std::filesystem::is_regular_file(found->second.TargetPath()))
        {
            throw std::runtime_error{ "This download is no longer on the device." };
        }
        PlaybackFiles result{ .VideoPath = found->second.TargetPath() };
        if (found->second.SubtitleFileName)
        {
            auto const subtitle = found->second.RootPath / *found->second.SubtitleFileName;
            if (IsWithinApprovedRoot(subtitle, resolvedRoots)
                && std::filesystem::is_regular_file(subtitle))
            {
                result.SubtitlePath = subtitle;
            }
        }
        return result;
    }

    std::filesystem::path TransferEngine::DownloadDirectory() const
    {
        return m_store.DownloadDirectory();
    }

    void TransferEngine::SetDownloadDirectory(std::filesystem::path directory)
    {
        m_store.SetDownloadDirectory(std::move(directory));
    }

    std::optional<std::uint64_t> TransferEngine::FreeBytes() const noexcept
    {
        try
        {
            return DiskFree(m_store.DownloadDirectory());
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    DownloadChangedToken TransferEngine::AddChangedHandler(DownloadChangedHandler handler)
    {
        if (!handler)
        {
            throw std::invalid_argument{ "A download changed handler is required." };
        }
        std::scoped_lock const lock{ m_mutex };
        auto const token = m_nextHandlerToken++;
        m_handlers.emplace(token, std::move(handler));
        return token;
    }

    void TransferEngine::RemoveChangedHandler(DownloadChangedToken token) noexcept
    {
        std::scoped_lock const lock{ m_mutex };
        m_handlers.erase(token);
    }

    void TransferEngine::Worker(std::stop_token stopToken)
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        while (!stopToken.stop_requested())
        {
            std::wstring jobId;
            std::unique_ptr<::HaloDesktop::Storage::FileMutationLock> jobLease;
            std::shared_ptr<std::atomic_bool> cancel;
            std::vector<DownloadRecord> snapshot;
            std::uint64_t generation{};
            DownloadRecord started;
            std::vector<DownloadChangedHandler> startedHandlers;
            {
                std::unique_lock lock{ m_mutex };
                m_condition.wait(lock, stopToken, [this]
                {
                    return !m_activeJob && m_activeAccount && !m_queue.empty();
                });
                if (stopToken.stop_requested())
                {
                    break;
                }
                while (!m_queue.empty())
                {
                    jobId = std::move(m_queue.front());
                    m_queue.pop_front();
                    auto const found = m_records.find(jobId);
                    if (found != m_records.end()
                        && m_activeAccount
                        && found->second.AccountKey == *m_activeAccount
                        && found->second.Status == DownloadStatus::Queued
                        && !found->second.PendingDeletion
                        && !found->second.ExplicitPause)
                    {
                        break;
                    }
                    jobId.clear();
                }
                if (jobId.empty())
                {
                    continue;
                }
            }
            try
            {
                jobLease = std::make_unique<::HaloDesktop::Storage::FileMutationLock>(
                    m_store.Paths().DataRoot / (L"download-job-" + jobId),
                    std::chrono::milliseconds{ 0 });
            }
            catch (...)
            {
                {
                    std::scoped_lock const lock{ m_mutex };
                    auto const found = m_records.find(jobId);
                    if (found != m_records.end()
                        && m_activeAccount
                        && found->second.AccountKey == *m_activeAccount
                        && found->second.Status == DownloadStatus::Queued
                        && !found->second.ExplicitPause)
                    {
                        m_queue.push_back(jobId);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{ 100 });
                continue;
            }
            std::vector<DownloadRecord> latest;
            try
            {
                latest = m_store.Load(false);
            }
            catch (...)
            {
                // The index is held open by another program, its lock timed out
                // or a corrupt copy could not be rewritten. Nothing changed, so
                // the job waits its turn again, as when its lease is taken.
                jobLease.reset();
                {
                    std::scoped_lock const lock{ m_mutex };
                    auto const found = m_records.find(jobId);
                    if (found != m_records.end()
                        && m_activeAccount
                        && found->second.AccountKey == *m_activeAccount
                        && found->second.Status == DownloadStatus::Queued
                        && !found->second.ExplicitPause
                        && std::find(m_queue.begin(), m_queue.end(), jobId) == m_queue.end())
                    {
                        m_queue.push_back(jobId);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{ 100 });
                continue;
            }
            {
                std::scoped_lock const lock{ m_mutex };
                auto const persisted = std::find_if(latest.begin(), latest.end(), [&jobId](DownloadRecord const& record)
                {
                    return record.JobId == jobId;
                });
                if (persisted == latest.end())
                {
                    m_records.erase(jobId);
                    continue;
                }
                auto refreshed = *persisted;
                if (refreshed.Status == DownloadStatus::Downloading)
                {
                    // Acquiring the lease proves no live worker owns this job.
                    // A persisted downloading state therefore came from an
                    // interrupted or exited process and is safe to resume.
                    refreshed.Status = DownloadStatus::Queued;
                    refreshed.BytesPerSecond = 0;
                }
                if (auto const live = m_records.find(jobId);
                    live != m_records.end() && live->second.PendingDeletion)
                {
                    // Remove tombstones memory before it stores the tombstone, so
                    // a stored copy read just before then must not undo it.
                    refreshed.PendingDeletion = true;
                    refreshed.Status = DownloadStatus::Paused;
                    refreshed.ExplicitPause = true;
                }
                else if (live != m_records.end()
                    && live->second.Status == DownloadStatus::Paused
                    && IsActive(refreshed.Status))
                {
                    // The job was queued in memory when it was taken, so only a
                    // pause made since (Pause, a replacement) can have set this,
                    // ahead of storing it: the stored copy read here may predate
                    // it and must not undo it.
                    refreshed.Status = DownloadStatus::Paused;
                    refreshed.ExplicitPause = live->second.ExplicitPause;
                    refreshed.BytesPerSecond = 0;
                }
                refreshed.ActiveConnections = 0;
                m_records.insert_or_assign(jobId, std::move(refreshed));
                auto const found = m_records.find(jobId);
                if (m_activeJob
                    || found == m_records.end()
                    || !m_activeAccount
                    || found->second.AccountKey != *m_activeAccount
                    || found->second.Status != DownloadStatus::Queued
                    || found->second.ExplicitPause
                    || found->second.PendingDeletion)
                {
                    continue;
                }
                cancel = std::make_shared<std::atomic_bool>(false);
                m_activeJob = jobId;
                m_cancel[jobId] = cancel;
                auto& record = m_records.at(jobId);
                record.Status = DownloadStatus::Downloading;
                record.UpdatedAt = NowMilliseconds();
                generation = ++m_generation;
                snapshot = { record };
                started = record;
                startedHandlers = HandlersLocked();
            }
            try
            {
                Persist(std::move(snapshot), generation);
                // Announced once it is stored, and outside the lock, as Finish and
                // the progress reports are. A transfer that ends before its first
                // progress report would otherwise never be seen downloading.
                Notify(startedHandlers, started);
                // The outcome is settled before Finish runs, so a Finish that
                // throws is never followed by a second Finish that reports a
                // different outcome (a finished file indexed as failed).
                std::optional<TransferResult> result;
                std::optional<TransferError> failure;
                try
                {
                    result = TransferWithRetries(jobId, cancel, stopToken);
                }
                catch (TransferError const& error)
                {
                    failure = error;
                }
                catch (...)
                {
                    failure = TransferError{
                        .Type = stopToken.stop_requested()
                            ? TransferError::Kind::Shutdown
                            : TransferError::Kind::Permanent,
                        .Failure = DownloadFailureCode::Unknown,
                    };
                }
                Finish(started, std::move(result), failure);
            }
            catch (...)
            {
                DownloadRecord changed;
                bool hasChanged{};
                std::vector<DownloadRecord> recoverySnapshot;
                std::vector<DownloadChangedHandler> handlers;
                std::uint64_t recoveryGeneration{};
                {
                    std::scoped_lock const lock{ m_mutex };
                    if (auto const found = m_records.find(jobId); found != m_records.end())
                    {
                        found->second.BytesPerSecond = 0;
                        found->second.ActiveConnections = 0;
                        found->second.UpdatedAt = NowMilliseconds();
                        if (!stopToken.stop_requested()
                            && found->second.Status == DownloadStatus::Downloading)
                        {
                            found->second.Status = DownloadStatus::Failed;
                            found->second.Failure = DownloadFailureCode::Unknown;
                        }
                        changed = found->second;
                        hasChanged = true;
                    }
                    m_cancel.erase(jobId);
                    m_activeJob.reset();
                    recoveryGeneration = ++m_generation;
                    if (hasChanged)
                    {
                        recoverySnapshot = { changed };
                    }
                    handlers = HandlersLocked();
                }
                if (hasChanged && changed.PendingDeletion)
                {
                    // Removed while it ran: nothing else will delete its files
                    // before the next launch.
                    try
                    {
                        std::scoped_lock const gate{ m_removalGate };
                        FinishRemoved(jobId, changed, changed);
                    }
                    catch (...)
                    {
                    }
                    m_condition.notify_all();
                    continue;
                }
                try { Persist(std::move(recoverySnapshot), recoveryGeneration); } catch (...) {}
                if (hasChanged) Notify(handlers, changed);
                m_condition.notify_all();
            }
        }
        winrt::uninit_apartment();
    }

    TransferEngine::TransferResult TransferEngine::TransferWithRetries(
        std::wstring const& jobId,
        std::shared_ptr<std::atomic_bool> const& cancel,
        std::stop_token stopToken)
    {
        ProtectedRequest request;
        try
        {
            request = m_vault.Read(jobId);
        }
        catch (...)
        {
            throw TransferError{
                .Type = TransferError::Kind::Permanent,
                .Failure = DownloadFailureCode::ProtectedRequestCorrupt,
            };
        }
        for (int attempt = 0; attempt < TransferAttempts; ++attempt)
        {
            try
            {
                return TransferOnce(jobId, request, cancel, stopToken);
            }
            catch (TransferError const& error)
            {
                if (error.Type != TransferError::Kind::Retryable || attempt + 1 == TransferAttempts)
                {
                    throw;
                }
                auto const delay = std::chrono::milliseconds{ 250 * (1 << attempt) };
                auto elapsed = std::chrono::milliseconds::zero();
                while (elapsed < delay)
                {
                    if (stopToken.stop_requested())
                    {
                        throw TransferError{ .Type = TransferError::Kind::Shutdown };
                    }
                    if (cancel->load(std::memory_order_acquire))
                    {
                        throw TransferError{ .Type = TransferError::Kind::Paused };
                    }
                    auto const slice = (std::min)(std::chrono::milliseconds{ 25 }, delay - elapsed);
                    std::this_thread::sleep_for(slice);
                    elapsed += slice;
                }
            }
        }
        throw TransferError{
            .Type = TransferError::Kind::Retryable,
            .Failure = DownloadFailureCode::Network,
        };
    }

    TransferEngine::TransferResult TransferEngine::TransferOnce(
        std::wstring const& jobId,
        ProtectedRequest const& request,
        std::shared_ptr<std::atomic_bool> const& cancel,
        std::stop_token stopToken)
    {
        DownloadRecord record;
        {
            std::scoped_lock const lock{ m_mutex };
            auto const found = m_records.find(jobId);
            if (found == m_records.end())
            {
                throw TransferError{ .Type = TransferError::Kind::Permanent };
            }
            record = found->second;
        }
        SegmentedJob const job{
            .Url = request.Url,
            .Headers = request.Headers,
            .Part = record.PartialPath(),
            .Root = record.RootPath,
            .KnownTotal = record.TotalBytes,
            .Validator = record.Validator,
            .SizeHint = record.Media.VideoSize,
            // Every run stores its starting count, and the count is otherwise
            // stored only after the part is flushed (a durable report, or the
            // end of a run, which flushes first), so it belongs to this record
            // and its validator.
            .FlushedBytes = record.DownloadedBytes,
        };
        SegmentedHost const host{
            .ResponseMetadata = [this, &jobId](std::optional<std::wstring> const& validator, std::uint64_t total)
            {
                UpdateResponseMetadata(jobId, validator, total);
            },
            .Progress = [this, &jobId](SegmentedProgress const& progress)
            {
                UpdateProgress(jobId, progress);
            },
        };
        SegmentedFinished finished;
        if (FinishedFileInPlace(record))
        {
            // An earlier run renamed the complete file into place and stopped
            // before its end was stored. Nothing is downloaded again.
            finished = SegmentedFinished{
                .Bytes = record.TotalBytes,
                .Total = record.TotalBytes,
                .Validator = record.Validator,
            };
        }
        else
        {
            try
            {
                finished = RunSegmentedTransfer(job, *cancel, stopToken, host, m_options.Transfer);
            }
            catch (TransferFailure const& failure)
            {
                auto kind = TransferError::Kind::Permanent;
                switch (failure.Kind)
                {
                case TransferFailureKind::Permanent: kind = TransferError::Kind::Permanent; break;
                case TransferFailureKind::Retryable: kind = TransferError::Kind::Retryable; break;
                case TransferFailureKind::Paused: kind = TransferError::Kind::Paused; break;
                case TransferFailureKind::Shutdown: kind = TransferError::Kind::Shutdown; break;
                }
                throw TransferError{ .Type = kind, .Failure = failure.Code };
            }
            if (!MoveFileExW(
                record.PartialPath().c_str(),
                record.TargetPath().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                throw TransferError{
                    .Type = TransferError::Kind::Permanent,
                    .Failure = DownloadFailureCode::StorageFull,
                };
            }
        }
        auto const total = (std::max)(finished.Total, finished.Bytes);
        std::optional<std::pair<std::wstring, std::wstring>> subtitle;
        if (request.Subtitle && !stopToken.stop_requested()
            && !cancel->load(std::memory_order_acquire))
        {
            subtitle = DownloadSubtitle(*request.Subtitle, record.TargetPath(), cancel, stopToken);
        }
        if (request.Subtitle && !subtitle
            && (stopToken.stop_requested() || cancel->load(std::memory_order_acquire)))
        {
            // A pause or exit stopped the subtitle, not a failure of it. Done
            // now would drop the request it needs for good, so the run ends as
            // paused (or interrupted) instead, with the video's size recorded:
            // the next run finds the file in place and only fetches the
            // subtitle (FinishedFileInPlace).
            {
                std::scoped_lock const lock{ m_mutex };
                if (auto const found = m_records.find(jobId); found != m_records.end())
                {
                    found->second.TotalBytes = total;
                    found->second.DownloadedBytes = total;
                    found->second.Validator = finished.Validator;
                }
            }
            throw TransferError{
                .Type = stopToken.stop_requested() ? TransferError::Kind::Shutdown : TransferError::Kind::Paused,
            };
        }
        return TransferResult{
            .Bytes = finished.Bytes,
            .Total = total,
            .Validator = std::move(finished.Validator),
            .Subtitle = std::move(subtitle),
        };
    }

    std::optional<std::pair<std::wstring, std::wstring>> TransferEngine::DownloadSubtitle(
        SubtitleRequest const& request,
        std::filesystem::path const& target,
        std::shared_ptr<std::atomic_bool> const& cancel,
        std::stop_token stopToken) const noexcept
    {
        try
        {
            auto language = SafeLanguage(request.Language);
            auto name = target.stem().wstring() + L"." + language + L"." + SubtitleExtension(request.Url);
            auto const path = target.parent_path() / name;
            auto temporary = path;
            temporary += L".part";
            auto cleanup = wil::scope_exit([&temporary]() noexcept
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
            });
            std::atomic_bool opening{ true };
            bool saved{};
            // Runs on a watched thread, so a pause or shutdown closes its request
            // instead of waiting for a slow subtitle host.
            auto const work = [&request, &temporary, &path, &opening, &saved](Http::Cancellation& cancellation)
            {
                try
                {
                    auto response = Http::OpenGet(request.Url, request.Headers, std::nullopt, std::nullopt, cancellation);
                    opening.store(false, std::memory_order_release);
                    if (response.Head.Status != 200
                        || (response.Head.ContentLength && *response.Head.ContentLength > MaximumSubtitleBytes))
                    {
                        return;
                    }
                    wil::unique_hfile output{ CreateFileW(
                        temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr) };
                    if (!output)
                    {
                        return;
                    }
                    std::vector<std::uint8_t> buffer(TransferChunkSize);
                    std::uint64_t written{};
                    for (;;)
                    {
                        std::size_t read{};
                        auto const outcome = Http::ReadSome(response, buffer, read, cancellation);
                        if (outcome == Http::ReadOutcome::Failed)
                        {
                            return;
                        }
                        if (outcome == Http::ReadOutcome::End)
                        {
                            break;
                        }
                        if (written + read > MaximumSubtitleBytes)
                        {
                            return;
                        }
                        DWORD chunkWritten{};
                        if (!WriteFile(output.get(), buffer.data(), static_cast<DWORD>(read), &chunkWritten, nullptr)
                            || chunkWritten != read)
                        {
                            return;
                        }
                        written += chunkWritten;
                    }
                    if (!FlushFileBuffers(output.get()) || cancellation.IsCancelled())
                    {
                        return;
                    }
                    output.reset();
                    saved = MoveFileExW(
                        temporary.c_str(), path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
                }
                catch (...)
                {
                }
            };
            static_cast<void>(Http::RunWatched(
                work,
                [&cancel]() { return cancel->load(std::memory_order_acquire); },
                [&stopToken]() { return stopToken.stop_requested(); },
                [&opening]() { return opening.load(std::memory_order_acquire); },
                m_options.Transfer.Timeouts.CancelPoll,
                m_options.SubtitleResponse));
            if (!saved)
            {
                return std::nullopt;
            }
            cleanup.release();
            return std::pair{ std::move(name), std::move(language) };
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    void TransferEngine::Finish(
        DownloadRecord const& runRecord,
        std::optional<TransferResult> result,
        std::optional<TransferError> error)
    {
        auto const& jobId = runRecord.JobId;
        std::scoped_lock const gate{ m_removalGate };
        DownloadRecord changed;
        bool hasChanged{};
        std::optional<DownloadRecord> replaced;
        std::optional<DownloadRecord> originalCurrent;
        std::optional<DownloadRecord> originalReplaced;
        std::vector<DownloadRecord> snapshot;
        std::vector<DownloadChangedHandler> handlers;
        std::uint64_t generation{};
        {
            std::unique_lock lock{ m_mutex };
            auto const found = m_records.find(jobId);
            if (found == m_records.end() || found->second.PendingDeletion)
            {
                // Removed while it ran, which Remove left to this run so nothing
                // was deleted while it still wrote. Whatever it finished, the
                // copy it was replacing stays.
                auto files = found != m_records.end() ? found->second : runRecord;
                if (result && result->Subtitle)
                {
                    files.SubtitleFileName = result->Subtitle->first;
                    files.SubtitleLanguage = result->Subtitle->second;
                }
                auto current = found != m_records.end()
                    ? std::optional<DownloadRecord>{ files }
                    : std::nullopt;
                m_cancel.erase(jobId);
                m_activeJob.reset();
                lock.unlock();
                FinishRemoved(jobId, std::move(current), files);
                m_condition.notify_all();
                return;
            }
            auto& record = found->second;
            originalCurrent = record;
            record.ActiveConnections = 0;
            if (result)
            {
                record.Status = DownloadStatus::Done;
                record.DownloadedBytes = result->Bytes;
                record.TotalBytes = result->Total;
                record.Validator = std::move(result->Validator);
                record.BytesPerSecond = 0;
                record.Failure.reset();
                record.ExplicitPause = false;
                if (result->Subtitle)
                {
                    record.SubtitleFileName = result->Subtitle->first;
                    record.SubtitleLanguage = result->Subtitle->second;
                }
                if (record.Replacement)
                {
                    if (auto const old = m_records.find(record.Replacement->JobId); old != m_records.end())
                    {
                        originalReplaced = old->second;
                        old->second.PendingDeletion = true;
                        old->second.Status = DownloadStatus::Paused;
                        old->second.ExplicitPause = true;
                        old->second.BytesPerSecond = 0;
                        old->second.UpdatedAt = NowMilliseconds();
                        replaced = old->second;
                    }
                }
            }
            else if (error && error->Type == TransferError::Kind::Paused)
            {
                record.Status = DownloadStatus::Paused;
                record.BytesPerSecond = 0;
            }
            else if (error && error->Type == TransferError::Kind::Shutdown)
            {
                record.BytesPerSecond = 0;
            }
            else
            {
                record.Status = DownloadStatus::Failed;
                record.Failure = error ? error->Failure : DownloadFailureCode::Unknown;
                record.BytesPerSecond = 0;
            }
            record.UpdatedAt = NowMilliseconds();
            if (result && replaced)
            {
                // Keep both the live and durable projections non-terminal until
                // the old files have either been removed or deliberately retained
                // behind their tombstone.
                record.Status = DownloadStatus::Downloading;
            }
            changed = record;
            hasChanged = true;
            m_cancel.erase(jobId);
            m_activeJob.reset();
            generation = ++m_generation;
            snapshot = { changed };
            if (replaced)
            {
                snapshot.push_back(*replaced);
            }
            handlers = HandlersLocked();
        }
        std::exception_ptr persistFailure;
        // A finished file is already in place, so its outcome is worth a few
        // more tries; any other outcome is given up at once as before.
        auto const attempts = result ? FinishPersistAttempts : 1;
        for (int attempt = 1;; ++attempt)
        {
            try
            {
                Persist(snapshot, generation);
                persistFailure = nullptr;
                break;
            }
            catch (...)
            {
                persistFailure = std::current_exception();
                if (attempt >= attempts)
                {
                    break;
                }
            }
            std::this_thread::sleep_for(FinishPersistRetry * attempt);
        }
        if (persistFailure && !result)
        {
            std::scoped_lock const lock{ m_mutex };
            if (originalCurrent)
            {
                m_records.insert_or_assign(originalCurrent->JobId, *originalCurrent);
            }
            if (originalReplaced)
            {
                m_records.insert_or_assign(originalReplaced->JobId, *originalReplaced);
            }
            std::rethrow_exception(persistFailure);
        }
        if (persistFailure)
        {
            // The file was renamed into place but the index still says
            // "downloading". It is shown as finished now; the index keeps the
            // old state, the request stays in the vault and the copy it
            // replaces stays hidden behind it, so the next run of this job
            // (the next launch, or an account switch back) finds the complete
            // file and finishes it without downloading again.
            OutputDebugStringW(L"Halo could not store a finished download; it is completed again at the next run.\n");
            {
                std::scoped_lock const lock{ m_mutex };
                if (originalReplaced)
                {
                    m_records.insert_or_assign(originalReplaced->JobId, *originalReplaced);
                }
                changed.Status = DownloadStatus::Done;
                m_records.insert_or_assign(changed.JobId, changed);
            }
            Notify(handlers, changed);
            m_condition.notify_all();
            return;
        }
        if (result || (error && RequiresNewSource(error->Failure)))
        {
            try { m_vault.Remove(jobId); } catch (...) {}
        }
        if (replaced)
        {
            try
            {
                RemoveRecordFiles(*replaced, m_store.ApprovedRoots(), hasChanged ? &changed : nullptr);
                m_vault.Remove(replaced->JobId);
                auto completed = changed;
                completed.Status = DownloadStatus::Done;
                completed.Replacement.reset();
                m_store.Apply({ completed }, { replaced->JobId });
                std::scoped_lock const lock{ m_mutex };
                m_records.insert_or_assign(completed.JobId, completed);
                m_records.erase(replaced->JobId);
                changed = std::move(completed);
            }
            catch (...)
            {
                OutputDebugStringW(L"Halo retained a replacement cleanup tombstone for retry.\n");
                auto completed = changed;
                completed.Status = DownloadStatus::Done;
                try { m_store.Apply({ completed }); } catch (...) {}
                std::scoped_lock const lock{ m_mutex };
                m_records.insert_or_assign(completed.JobId, completed);
                changed = std::move(completed);
            }
        }
        if (hasChanged)
        {
            Notify(handlers, changed);
        }
        m_condition.notify_all();
    }

    // Deletes what a run removed while it ran left behind: its request, its
    // files unless another record still names one of them, and the record. A
    // file outside the download folders is never touched; an I/O failure keeps
    // the tombstone so the next launch tries again. Called with the removal gate
    // held and m_mutex not held.
    void TransferEngine::FinishRemoved(
        std::wstring const& jobId,
        std::optional<DownloadRecord> current,
        DownloadRecord const& files)
    {
        bool shared{};
        {
            std::scoped_lock const lock{ m_mutex };
            try
            {
                shared = SharesFilesWithAnotherRecord(files, m_records);
            }
            catch (...)
            {
                // A path that cannot be compared is treated as shared: keeping a
                // file is recoverable, deleting another record's is not.
                shared = true;
            }
        }
        try
        {
            m_vault.Remove(jobId);
            if (!shared)
            {
                RemoveRecordFiles(files, m_store.ApprovedRoots());
            }
            m_store.Apply({}, { jobId });
            std::scoped_lock const lock{ m_mutex };
            m_records.erase(jobId);
        }
        catch (ContainmentError const&)
        {
            OutputDebugStringW(L"Halo forgot a deletion that pointed outside its download folders.\n");
            try { m_store.Apply({}, { jobId }); } catch (...) {}
            std::scoped_lock const lock{ m_mutex };
            m_records.erase(jobId);
        }
        catch (...)
        {
            OutputDebugStringW(L"Halo kept a removed download's tombstone to finish deleting it at launch.\n");
            if (current)
            {
                try { m_store.Apply({ *current }); } catch (...) {}
            }
        }
    }

    void TransferEngine::UpdateResponseMetadata(
        std::wstring const& jobId,
        std::optional<std::wstring> validator,
        std::uint64_t totalBytes)
    {
        std::vector<DownloadRecord> snapshot;
        std::uint64_t generation{};
        {
            std::scoped_lock const lock{ m_mutex };
            auto const found = m_records.find(jobId);
            if (found == m_records.end()) return;
            if (validator) found->second.Validator = std::move(validator);
            if (totalBytes > 0) found->second.TotalBytes = totalBytes;
            found->second.UpdatedAt = NowMilliseconds();
            generation = ++m_generation;
            snapshot = { found->second };
        }
        // Best effort, as every store during a transfer: memory holds the state
        // and the next durable progress report writes the whole record again. A
        // busy index must not fail a healthy download.
        try
        {
            Persist(std::move(snapshot), generation);
        }
        catch (...)
        {
        }
    }

    void TransferEngine::UpdateProgress(
        std::wstring const& jobId,
        SegmentedProgress const& progress)
    {
        DownloadRecord changed;
        std::vector<DownloadRecord> snapshot;
        std::vector<DownloadChangedHandler> handlers;
        std::uint64_t generation{};
        {
            std::scoped_lock const lock{ m_mutex };
            auto const found = m_records.find(jobId);
            // A removed download winding down reports nothing: it is no longer
            // anyone's to see.
            if (found == m_records.end() || found->second.PendingDeletion) return;
            found->second.DownloadedBytes = progress.Bytes;
            if (progress.Total > 0) found->second.TotalBytes = progress.Total;
            found->second.BytesPerSecond = progress.BytesPerSecond;
            found->second.ActiveConnections = found->second.Status == DownloadStatus::Downloading
                ? progress.Connections
                : 0;
            found->second.UpdatedAt = NowMilliseconds();
            changed = found->second;
            generation = ++m_generation;
            snapshot = { changed };
            handlers = HandlersLocked();
        }
        if (progress.Persist)
        {
            // Best effort: the count is kept in memory and stored again at the
            // next durable report, or by the end of the run.
            try
            {
                Persist(std::move(snapshot), generation);
            }
            catch (...)
            {
            }
        }
        Notify(handlers, changed);
    }

    std::vector<DownloadRecord> TransferEngine::SnapshotLocked() const
    {
        std::vector<DownloadRecord> result;
        result.reserve(m_records.size());
        for (auto const& [jobId, record] : m_records)
        {
            static_cast<void>(jobId);
            result.push_back(record);
        }
        return result;
    }

    std::vector<DownloadChangedHandler> TransferEngine::HandlersLocked() const
    {
        std::vector<DownloadChangedHandler> result;
        result.reserve(m_handlers.size());
        for (auto const& [token, handler] : m_handlers)
        {
            static_cast<void>(token);
            result.push_back(handler);
        }
        return result;
    }

    std::optional<DownloadRecord> TransferEngine::VisibleRecordForVideoLocked(
        std::wstring const& videoId) const
    {
        if (!m_activeAccount) return std::nullopt;
        for (auto const& [jobId, record] : m_records)
        {
            if (record.AccountKey == *m_activeAccount
                && record.Media.VideoId == videoId
                && !record.PendingDeletion
                && !IsHiddenBackupLocked(jobId))
            {
                return record;
            }
        }
        return std::nullopt;
    }

    bool TransferEngine::IsHiddenBackupLocked(std::wstring const& jobId) const
    {
        return std::any_of(m_records.begin(), m_records.end(), [&jobId](auto const& pair)
        {
            return !pair.second.PendingDeletion
                && pair.second.Replacement
                && pair.second.Replacement->JobId == jobId;
        });
    }

    void TransferEngine::Persist(
        std::vector<DownloadRecord> records,
        std::uint64_t generation)
    {
        static_cast<void>(generation);
        m_store.Apply(records);
    }

    void TransferEngine::Notify(
        std::vector<DownloadChangedHandler> const& handlers,
        DownloadRecord const& record) noexcept
    {
        for (auto const& handler : handlers)
        {
            try { handler(record); } catch (...) {}
        }
    }

    void TransferEngine::ReconcileMissingFiles()
    {
        struct Candidate final
        {
            std::wstring JobId;
            std::filesystem::path Root;
            std::filesystem::path Target;
            std::uint64_t TotalBytes{};
            bool MarkedMissing{};
        };
        auto const markedMissing = [](DownloadRecord const& record)
        {
            return record.Status == DownloadStatus::Failed
                && record.Failure == DownloadFailureCode::MissingFile;
        };
        std::vector<Candidate> candidates;
        std::vector<std::filesystem::path> approvedRoots;
        try
        {
            approvedRoots = m_store.ApprovedRoots();
        }
        catch (...)
        {
        }
        {
            std::scoped_lock const lock{ m_mutex };
            for (auto const& [jobId, record] : m_records)
            {
                if (record.PendingDeletion)
                {
                    continue;
                }
                auto const marked = markedMissing(record);
                if (record.Status == DownloadStatus::Done || marked)
                {
                    candidates.push_back(Candidate{
                        .JobId = jobId,
                        .Root = PresenceFolder(record.RootPath, approvedRoots),
                        .Target = record.TargetPath(),
                        .TotalBytes = record.TotalBytes,
                        .MarkedMissing = marked,
                    });
                }
            }
        }
        for (auto const& candidate : candidates)
        {
            // Read outside every lock. Only a folder that is there without the
            // file means the file is gone; an unplugged drive says nothing.
            auto const presence = DetectFilePresence(candidate.Root, candidate.Target);
            auto const nowMissing = !candidate.MarkedMissing && presence.Kind == FilePresenceKind::Missing;
            auto const returned = candidate.MarkedMissing
                && presence.Kind == FilePresenceKind::Present
                && presence.Bytes == candidate.TotalBytes;
            if (!nowMissing && !returned)
            {
                continue;
            }
            std::unique_ptr<::HaloDesktop::Storage::FileMutationLock> jobLease;
            try
            {
                jobLease = std::make_unique<::HaloDesktop::Storage::FileMutationLock>(
                    m_store.Paths().DataRoot / (L"download-job-" + candidate.JobId),
                    std::chrono::milliseconds{ 0 });
            }
            catch (...)
            {
                continue;
            }

            DownloadRecord changed;
            DownloadRecord previous;
            std::vector<DownloadChangedHandler> handlers;
            {
                std::scoped_lock const lock{ m_mutex };
                auto const found = m_records.find(candidate.JobId);
                if (found == m_records.end() || found->second.PendingDeletion)
                {
                    continue;
                }
                // Re-checked under the lock: the record may have moved on while
                // the disk was being read.
                previous = found->second;
                if (returned && markedMissing(found->second))
                {
                    found->second.Status = DownloadStatus::Done;
                    found->second.Failure.reset();
                }
                else if (nowMissing && found->second.Status == DownloadStatus::Done)
                {
                    found->second.Status = DownloadStatus::Failed;
                    found->second.Failure = DownloadFailureCode::MissingFile;
                    found->second.BytesPerSecond = 0;
                }
                else
                {
                    continue;
                }
                found->second.UpdatedAt = NowMilliseconds();
                changed = found->second;
                handlers = HandlersLocked();
            }
            try
            {
                m_store.Apply({ changed });
            }
            catch (...)
            {
                std::scoped_lock const lock{ m_mutex };
                m_records.insert_or_assign(previous.JobId, previous);
                continue;
            }
            Notify(handlers, changed);
        }
    }

    void RunDownloadEngineUnitChecks()
    {
        if (!RequiresNewSource(DownloadFailureCode::SourceExpired)
            || !RequiresNewSource(DownloadFailureCode::InvalidRange)
            || !RequiresNewSource(DownloadFailureCode::ProtectedRequestCorrupt)
            || !RequiresNewSource(DownloadFailureCode::MissingFile)
            || RequiresNewSource(DownloadFailureCode::Network))
        {
            throw std::runtime_error{ "The download failure retry policy is invalid." };
        }
        if (RecoverStatus(DownloadStatus::Downloading, false) != DownloadStatus::Queued
            || RecoverStatus(DownloadStatus::Downloading, true) != DownloadStatus::Paused)
        {
            throw std::runtime_error{ "The download recovery policy is invalid." };
        }
        auto const range = ParseContentRange(L"bytes 10-99/100");
        if (!range || range->Start != 10 || range->End != 99 || range->Total != 100
            || ParseContentRange(L"bytes 10-x/100")
            || ParseContentRange(L"bytes 10-100/100"))
        {
            throw std::runtime_error{ "The download content range parser is invalid." };
        }
        DownloadMedia media{ .VideoId = L"movie:1", .ItemId = L"movie:1", .MediaType = L"movie", .Title = L"Test", .FileName = L"../../bad name.exe" };
        auto const fingerprint = Sha256Hex(L"https://example.test/video");
        auto const firstAccount = MakeAccountKey(L"https://one.test", L"user");
        auto const secondAccount = MakeAccountKey(L"https://two.test", L"user");
        auto const name = MakeDownloadFileName(media, fingerprint, firstAccount);
        if (!IsSafeFileName(name) || !name.ends_with(L".mkv") || name.find(L"..") != std::wstring::npos)
        {
            throw std::runtime_error{ "The download filename policy is invalid." };
        }
        if (name == MakeDownloadFileName(media, fingerprint, secondAccount))
        {
            throw std::runtime_error{ "Two accounts resolved one download filename." };
        }
        ProtectedRequest denied{
            .Url = L"https://example.test/video",
            .Headers = { { L"Range", L"bytes=0-1" } },
        };
        bool rejected{};
        try { ValidateProtectedRequest(denied); } catch (std::invalid_argument const&) { rejected = true; }
        if (!rejected)
        {
            throw std::runtime_error{ "A caller-controlled range header was accepted." };
        }
        if (HasSufficientSpace(64ull * 1024ull * 1024ull + 99, 100)
            || !HasSufficientSpace(64ull * 1024ull * 1024ull + 100, 100))
        {
            throw std::runtime_error{ "The download free-space reserve is invalid." };
        }
        if (HasSufficientSpace(64ull * 1024ull * 1024ull - 1, 0)
            || !HasSufficientSpace(64ull * 1024ull * 1024ull, 0))
        {
            throw std::runtime_error{ "Unknown-size downloads did not enforce the free-space reserve." };
        }
    }
}
