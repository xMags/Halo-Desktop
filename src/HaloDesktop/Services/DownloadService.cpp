#include "pch.h"
#include "Services/DownloadService.h"

#include "Models/Models.h"
#include "Services/DevicePreferencesStore.h"
#include "Controls/ArtworkUrlPolicy.h"
#include "Services/DownloadArtworkService.h"
#include "Services/DownloadSidecarFetcher.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <shellapi.h>
#include <shlobj_core.h>
#include <iomanip>
#include <exception>
#include <limits>
#include <new>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <wil/resource.h>

namespace
{
    using HaloDesktop::Services::Downloads::DownloadRecord;
    using HaloDesktop::Services::Downloads::DownloadStatus;
}

namespace HaloDesktop::Services
{
    DownloadService::DownloadService(
        std::shared_ptr<Downloads::TransferEngine> engine,
        std::shared_ptr<ISessionService> session,
        std::shared_ptr<DevicePreferencesStore> devicePreferences,
        std::shared_ptr<DownloadArtworkService> artwork,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_engine(std::move(engine)),
          m_session(std::move(session)),
          m_devicePreferences(std::move(devicePreferences)),
          m_artwork(std::move(artwork)),
          m_dispatcher(std::move(dispatcher))
    {
        if (!m_engine || !m_session || !m_devicePreferences || !m_artwork || !m_dispatcher)
        {
            throw std::invalid_argument{ "DownloadService requires all dependencies." };
        }
    }

    DownloadService::~DownloadService()
    {
        Stop();
    }

    void DownloadService::Start()
    {
        if (m_running)
        {
            return;
        }
        m_engineToken = m_engine->AddChangedHandler([weak = weak_from_this()](Downloads::DownloadRecord const& record)
        {
            if (auto const self = weak.lock())
            {
                auto const accountVersion = self->m_accountVersion.load();
                // Stamped as the engine raises it, so a list read that began
                // before this point knows the event may be missing from it.
                auto const sequence = ++self->m_eventSequence;
                self->m_dispatcher.TryEnqueue([weak, record, accountVersion, sequence]() mutable
                {
                    if (auto const owner = weak.lock())
                    {
                        owner->ApplyEngineProgress(std::move(record), accountVersion, sequence);
                    }
                });
                self->RequestSynchronize();
            }
        });
        m_throughputTimer = m_dispatcher.CreateTimer();
        m_throughputTimer.Interval(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
            DownloadsLogic::ThroughputSampleInterval));
        m_throughputTimer.IsRepeating(true);
        m_throughputTickToken = m_throughputTimer.Tick(
            [weak = weak_from_this()](
                [[maybe_unused]] winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer const& timer,
                [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args)
            {
                if (auto const self = weak.lock())
                {
                    self->SampleThroughput();
                }
            });
        m_running = true;
        RebindAccount();
    }

    void DownloadService::Stop() noexcept
    {
        m_running = false;
        ++m_snapshotVersion;
        if (m_engineToken != 0)
        {
            m_engine->RemoveChangedHandler(m_engineToken);
            m_engineToken = 0;
        }
        try
        {
            if (m_throughputTimer)
            {
                m_throughputTimer.Stop();
                m_throughputTimer.Tick(m_throughputTickToken);
                m_throughputTickToken = {};
                m_throughputTimer = nullptr;
            }
        }
        catch (...)
        {
        }
    }

    void DownloadService::RebindAccount()
    {
        // The account's partition, or this PC's profile's without an account.
        auto const owner = m_session->DataOwner();
        auto const server = owner ? owner->Server : std::wstring{};
        auto const userId = owner ? owner->User : std::wstring{};
        auto const accountVersion = ++m_accountVersion;
        auto const version = ++m_snapshotVersion;
        m_snapshotFloor.store(version);
        m_records.clear();
        m_pauseAllIds.clear();
        m_artworkPending.clear();
        m_sidecarsTried.clear();
        m_artwork->OnAccountChanged();
        m_freeBytes.reset();
        m_directory.clear();
        m_actionError.clear();
        m_throughput.clear();
        m_lastEventSequence.clear();
        m_appliedSince = 0;
        RecordsChanged();
        auto engine = m_engine;
        auto weak = weak_from_this();
        concurrency::create_task([engine, server, userId, weak, version, accountVersion]()
        {
            Snapshot snapshot;
            try
            {
                auto const self = weak.lock();
                if (!self)
                {
                    return;
                }
                std::scoped_lock const accountLock{ self->m_accountMutex };
                if (accountVersion != self->m_accountVersion.load())
                {
                    return;
                }
                if (userId.empty())
                {
                    engine->ClearAccount();
                }
                else
                {
                    engine->SetAccount(server, userId);
                    // Downloads saved under another account key (an earlier
                    // server address, a reinstall) are this viewer's too: they
                    // join this account rather than sitting hidden on disk. A
                    // failure leaves them where they are for the next bind.
                    try
                    {
                        engine->AdoptOtherOwners();
                    }
                    catch (...)
                    {
                        OutputDebugStringW(L"Halo could not bring downloads saved under another account into this one; it tries again next time.\n");
                    }
                }
                if (accountVersion != self->m_accountVersion.load())
                {
                    engine->ClearAccount();
                    return;
                }
                self->m_boundAccountVersion.store(accountVersion);
                snapshot.Since = self->m_eventSequence.load();
                snapshot.Records = engine->List();
                snapshot.FreeBytes = engine->FreeBytes();
                snapshot.Directory = engine->DownloadDirectory();
            }
            catch (...)
            {
                snapshot.Records.clear();
            }
            if (auto const self = weak.lock())
            {
                self->m_dispatcher.TryEnqueue([weak, snapshot = std::move(snapshot), version]() mutable
                {
                    if (auto const owner = weak.lock())
                    {
                        owner->ApplySnapshot(std::move(snapshot), version);
                    }
                });
            }
        });
    }

    // The tracked IDs preserve the distinction between transfers paused by the
    // bulk action and transfers the user paused individually. Still-paused IDs
    // survive a new transfer appearing while the bulk action is in effect.
    void DownloadService::PauseAll()
    {
        auto plan = DownloadsLogic::PlanPauseAll(PausableRecords(), m_pauseAllIds);
        m_pauseAllIds = std::move(plan.Remembered);
        auto ids = std::move(plan.Pause);
        NotifyChanged();
        if (ids.empty())
        {
            return;
        }
        RunEngineAction([engine = m_engine, ids = std::move(ids)]()
        {
            // Attempt every transfer, then surface the first failure.
            std::exception_ptr failure;
            for (auto const& id : ids)
            {
                try { engine->Pause(id); }
                catch (...) { if (!failure) failure = std::current_exception(); }
            }
            if (failure) std::rethrow_exception(failure);
        });
    }

    void DownloadService::ResumeAll()
    {
        if (!IsPausedAll())
        {
            return;
        }
        auto ids = DownloadsLogic::PlanResumeAll(PausableRecords(), m_pauseAllIds);
        m_pauseAllIds.clear();
        NotifyChanged();
        RunEngineAction([engine = m_engine, ids = std::move(ids)]()
        {
            std::exception_ptr failure;
            for (auto const& id : ids)
            {
                try { engine->Resume(id); }
                catch (...) { if (!failure) failure = std::current_exception(); }
            }
            if (failure) std::rethrow_exception(failure);
        });
    }

    bool DownloadService::PauseTransfer(winrt::hstring const& id)
    {
        auto const record = FindRecord(id);
        if (!record || !Downloads::IsActive(record->Status))
        {
            return false;
        }
        RunEngineAction([engine = m_engine, jobId = std::wstring{ id.c_str() }]() { engine->Pause(jobId); });
        return true;
    }

    bool DownloadService::ResumeTransfer(winrt::hstring const& id)
    {
        auto const record = FindRecord(id);
        if (!record || (record->Status != Downloads::DownloadStatus::Paused
            && record->Status != Downloads::DownloadStatus::Failed))
        {
            return false;
        }
        RunEngineAction([engine = m_engine, jobId = std::wstring{ id.c_str() }]() { engine->Resume(jobId); });
        return true;
    }

    bool DownloadService::CancelTransfer(winrt::hstring const& id)
    {
        auto const record = FindRecord(id);
        if (!record || record->Status == Downloads::DownloadStatus::Done)
        {
            return false;
        }
        // This copy can be stale: the engine decides again, against the end of
        // the run, and leaves a download that finished first in place.
        RunEngineAction([engine = m_engine, jobId = std::wstring{ id.c_str() }]()
        {
            static_cast<void>(engine->Cancel(jobId));
        });
        return true;
    }

    bool DownloadService::DeleteReady(winrt::hstring const& id)
    {
        auto const record = FindRecord(id);
        if (!record || record->Status != Downloads::DownloadStatus::Done)
        {
            return false;
        }
        RunEngineAction([engine = m_engine, jobId = std::wstring{ id.c_str() }]() { engine->Remove(jobId); });
        return true;
    }

    std::size_t DownloadService::DeleteAllDownloads()
    {
        std::vector<std::pair<std::wstring, bool>> jobs;
        for (auto const& record : m_records)
        {
            jobs.emplace_back(record.JobId, record.Status == DownloadStatus::Done);
        }
        if (jobs.empty()) return 0;
        auto const count = jobs.size();
        RunEngineAction([engine = m_engine, jobs = std::move(jobs)]()
        {
            std::exception_ptr failure;
            for (auto const& [id, done] : jobs)
            {
                try
                {
                    // This copy can be stale. An unfinished one is cancelled
                    // under the engine's own check; if it finished first, the
                    // viewer still asked for everything to go, so it goes too.
                    if (done || !engine->Cancel(id)) engine->Remove(id);
                }
                catch (...)
                {
                    if (!failure) failure = std::current_exception();
                }
            }
            if (failure) std::rethrow_exception(failure);
        });
        return count;
    }

    void DownloadService::RetryFailedTransfers()
    {
        std::vector<std::wstring> ids;
        for (auto const& record : m_records)
        {
            if (record.Status == DownloadStatus::Failed
                && (!record.Failure || !Downloads::RequiresNewSource(*record.Failure)))
            {
                ids.push_back(record.JobId);
            }
        }
        if (ids.empty()) return;
        RunEngineAction([engine = m_engine, ids = std::move(ids)]()
        {
            std::exception_ptr failure;
            for (auto const& id : ids)
            {
                try { engine->Resume(id); }
                catch (...) { if (!failure) failure = std::current_exception(); }
            }
            if (failure) std::rethrow_exception(failure);
        });
    }

    bool DownloadService::OpenDownloadDirectory()
    {
        if (m_directory.empty())
        {
            m_actionError = L"The download folder is not available yet.";
            NotifyChanged();
            return false;
        }
        auto const result = ::ShellExecuteW(nullptr, L"open", m_directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<std::intptr_t>(result) <= 32)
        {
            m_actionError = L"The download folder could not be opened.";
            NotifyChanged();
            return false;
        }
        return true;
    }

    bool DownloadService::RevealInExplorer(winrt::hstring const& id)
    {
        auto const record = FindRecord(id);
        if (!record || record->Status != DownloadStatus::Done || record->PendingDeletion)
        {
            return false;
        }
        std::filesystem::path video;
        try
        {
            // The same resolution playback uses, which checks the file is still
            // there and belongs to this account before anything is shown.
            video = m_engine->FilesForPlayback(record->JobId).VideoPath;
        }
        catch (...)
        {
            return false;
        }
        PIDLIST_ABSOLUTE item{};
        if (FAILED(::SHParseDisplayName(video.c_str(), nullptr, &item, 0, nullptr)) || !item)
        {
            return false;
        }
        auto const releaseItem = wil::scope_exit([item]() noexcept { ::ILFree(item); });
        return SUCCEEDED(::SHOpenFolderAndSelectItems(item, 0, nullptr, 0));
    }

    concurrency::task<DownloadStartResult> DownloadService::StartDownloadAsync(
        Downloads::DownloadStartRequest request)
    {
        auto const owner = m_session->DataOwner();
        auto const server = owner ? owner->Server : std::wstring{};
        auto const userId = owner ? owner->User : std::wstring{};
        auto const accountVersion = m_accountVersion.load();
        if (server.empty() || userId.empty())
        {
            co_return DownloadStartResult{ DownloadStartOutcome::Failed, L"Sign in before downloading." };
        }
        auto const videoId = request.Media.VideoId;
        auto const fingerprint = Downloads::Sha256Hex(request.Request.Url);
        auto const replacing = request.ReplaceExisting;
        try
        {
            co_await winrt::resume_background();
            std::scoped_lock const accountLock{ m_accountMutex };
            if (accountVersion != m_accountVersion.load())
            {
                co_return DownloadStartResult{ DownloadStartOutcome::Failed, L"The active account changed while starting the download." };
            }
            m_engine->SetAccount(server, userId);
            if (accountVersion != m_accountVersion.load())
            {
                m_engine->ClearAccount();
                co_return DownloadStartResult{ DownloadStartOutcome::Failed, L"The active account changed while starting the download." };
            }
            m_boundAccountVersion.store(accountVersion);
            auto const before = m_engine->List();
            auto const existing = std::find_if(before.begin(), before.end(), [&videoId](auto const& record)
            {
                return record.Media.VideoId == videoId;
            });
            if (existing != before.end()
                && existing->SourceFingerprint == fingerprint
                && existing->Status != Downloads::DownloadStatus::Failed)
            {
                RequestSynchronize();
                co_return DownloadStartResult{ DownloadStartOutcome::AlreadyExists, {} };
            }
            if (existing != before.end()
                && !replacing
                && (existing->SourceFingerprint != fingerprint
                    || (existing->Failure && Downloads::RequiresNewSource(*existing->Failure))))
            {
                co_return DownloadStartResult{ DownloadStartOutcome::ReplacementRequired, {} };
            }
            // The engine refuses a start with a sentence written for the viewer
            // ("There is not enough free space for this video."), which is passed
            // on unchanged. Operating system and allocation failures carry text
            // that is not, so they are reported without one.
            std::optional<std::wstring> refusal;
            try
            {
                static_cast<void>(m_engine->Start(std::move(request)));
            }
            catch (std::system_error const&)
            {
                throw;
            }
            catch (std::bad_alloc const&)
            {
                throw;
            }
            catch (std::exception const& error)
            {
                refusal = std::wstring{ winrt::to_hstring(error.what()).c_str() };
            }
            if (refusal)
            {
                RequestSynchronize();
                co_return DownloadStartResult{ DownloadStartOutcome::Failed, std::move(*refusal) };
            }
            // The account changed while the engine created the download. The
            // reference makes that check just before it commits and refuses
            // with this sentence; this engine commits inside Start, so the new
            // download stays with the account that asked for it and comes back
            // when that account does. The current account did not get it, so
            // the sheet is told why rather than shown a reasonless failure.
            if (accountVersion != m_accountVersion.load())
            {
                m_engine->ClearAccount();
                co_return DownloadStartResult{ DownloadStartOutcome::Failed, L"The active account changed while starting the download." };
            }
            RequestSynchronize();
            co_return DownloadStartResult{ DownloadStartOutcome::Started, {} };
        }
        catch (...)
        {
            RequestSynchronize();
            co_return DownloadStartResult{};
        }
    }

    winrt::HaloDesktop::PlaybackRequest DownloadService::BuildPlaybackRequest(winrt::hstring const& id) const
    {
        auto const record = FindRecord(id);
        return record ? BuildPlaybackRequest(*record) : nullptr;
    }

    winrt::HaloDesktop::SourcesNavParams DownloadService::BuildSourcesNavigation(winrt::hstring const& id) const
    {
        auto const record = FindRecord(id);
        if (!record)
        {
            return nullptr;
        }
        auto const& media = record->Media;
        auto const sources = winrt::make<winrt::HaloDesktop::implementation::SourcesNavParams>(
            winrt::hstring{ media.MediaType },
            winrt::hstring{ media.MetaId.value_or(L"") },
            winrt::hstring{ media.VideoId },
            winrt::hstring{ media.ItemId },
            winrt::hstring{ media.Title },
            winrt::hstring{ media.ShowName.value_or(L"") },
            winrt::hstring{ media.EpisodeLabel.value_or(L"") },
            winrt::hstring{ media.Poster.value_or(L"") });
        sources.ReleaseInfo(winrt::hstring{ media.Year.value_or(L"") });
        return sources;
    }

    std::optional<winrt::HaloDesktop::PlaybackRequest> DownloadService::OfflineNext(
        winrt::hstring const& id) const
    {
        auto const current = FindRecord(id);
        if (!current || current->Media.MediaType != L"series")
        {
            return std::nullopt;
        }
        auto const position = Downloads::ParseEpisodePosition(current->Media.EpisodeLabel);
        if (!position)
        {
            return std::nullopt;
        }
        DownloadRecord const* next{};
        std::pair<int, int> nextPosition{ (std::numeric_limits<int>::max)(), (std::numeric_limits<int>::max)() };
        for (auto const& candidate : m_records)
        {
            if (candidate.Status != DownloadStatus::Done
                || candidate.Media.ItemId != current->Media.ItemId)
            {
                continue;
            }
            auto const candidatePosition = Downloads::ParseEpisodePosition(candidate.Media.EpisodeLabel);
            if (candidatePosition && *candidatePosition > *position && *candidatePosition < nextPosition)
            {
                next = &candidate;
                nextPosition = *candidatePosition;
            }
        }
        if (!next)
        {
            return std::nullopt;
        }
        auto request = BuildPlaybackRequest(*next);
        return request ? std::optional{ request } : std::nullopt;
    }

    std::optional<std::filesystem::path> DownloadService::SubtitlePath(winrt::hstring const& id) const
    {
        try
        {
            return m_engine->FilesForPlayback(std::wstring{ id.c_str() }).SubtitlePath;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool DownloadService::IsRunning() const noexcept { return m_running; }
    // Paused as a page means nothing is moving and something is waiting to be
    // told to move again. Queued counts as moving: it starts on its own.
    bool DownloadService::IsPausedAll() const noexcept
    {
        try
        {
            return DownloadsLogic::IsPausedAll(PausableRecords(), m_pauseAllIds);
        }
        catch (...)
        {
            return false;
        }
    }
    bool DownloadService::HasCompleted(winrt::hstring const& videoId) const noexcept
    {
        return std::any_of(m_records.begin(), m_records.end(), [&videoId](auto const& record)
        {
            return record.Status == DownloadStatus::Done && record.Media.VideoId == videoId.c_str();
        });
    }

    std::vector<Downloads::CompletedDownloadSource> DownloadService::CompletedFor(
        winrt::hstring const& videoId) const
    {
        std::vector<Downloads::CompletedDownloadSource> result;
        if (videoId.empty())
        {
            return result;
        }
        for (auto const& record : m_records)
        {
            if (record.Status != DownloadStatus::Done
                || record.PendingDeletion
                || record.Media.VideoId != videoId.c_str())
            {
                continue;
            }
            // TotalBytes is the size the transfer agreed with the server and is the
            // one worth showing; a record that never learned it falls back to what
            // actually landed on disk.
            result.push_back(Downloads::CompletedDownloadSource{
                .JobId = record.JobId,
                .ReleaseName = record.Media.FileName.value_or(record.FileName),
                .SizeBytes = record.TotalBytes > 0 ? record.TotalBytes : record.DownloadedBytes });
        }
        return result;
    }

    std::vector<Downloads::VideoDownload> DownloadService::DownloadsFor(winrt::hstring const& videoId) const
    {
        std::vector<Downloads::VideoDownload> result;
        if (videoId.empty())
        {
            return result;
        }
        for (auto const& record : m_records)
        {
            if (record.PendingDeletion || record.Media.VideoId != videoId.c_str())
            {
                continue;
            }
            auto const total = record.TotalBytes;
            result.push_back(Downloads::VideoDownload{
                .JobId = record.JobId,
                .ReleaseName = record.Media.FileName && !record.Media.FileName->empty()
                    ? *record.Media.FileName
                    : record.FileName,
                .StreamName = record.Media.StreamName,
                .StreamTitle = record.Media.StreamTitle,
                .Status = record.Status,
                .SizeBytes = (std::max)(total, record.DownloadedBytes),
                .Fraction = total > 0
                    ? std::optional<double>{ (std::min)(1.0, static_cast<double>(record.DownloadedBytes) / static_cast<double>(total)) }
                    : std::nullopt,
                .SubtitleLanguage = record.SubtitleLanguage,
            });
        }
        return result;
    }

    std::int32_t DownloadService::ActiveCount() const noexcept
    {
        return static_cast<std::int32_t>(std::count_if(m_records.begin(), m_records.end(), [](auto const& record)
        {
            return Downloads::IsActive(record.Status);
        }));
    }
    std::vector<Downloads::DownloadRecord> const& DownloadService::Records() const noexcept { return m_records; }
    std::vector<double> const& DownloadService::Throughput() const noexcept { return m_throughput; }
    std::optional<std::uint64_t> DownloadService::FreeBytes() const noexcept { return m_freeBytes; }
    std::filesystem::path DownloadService::DownloadDirectory() const { return m_directory; }

    concurrency::task<void> DownloadService::SetDownloadDirectoryAsync(std::filesystem::path directory)
    {
        co_await winrt::resume_background();
        m_engine->SetDownloadDirectory(std::move(directory));
        co_await wil::resume_foreground(m_dispatcher);
        m_actionError.clear();
        RequestSynchronize();
    }

    winrt::hstring DownloadService::ActionError() const { return m_actionError; }

    DownloadChangedToken DownloadService::AddChangedHandler(DownloadChangedHandler handler)
    {
        if (!handler)
        {
            throw std::invalid_argument{ "A download changed handler is required." };
        }
        auto const token = m_nextToken++;
        m_handlers.emplace(token, std::move(handler));
        return token;
    }

    void DownloadService::RemoveChangedHandler(DownloadChangedToken token) noexcept
    {
        m_handlers.erase(token);
    }

    concurrency::task<void> DownloadService::AdoptAccountAsync(std::wstring from, std::wstring to)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const engine = m_engine;
        auto const weak = weak_from_this();
        co_await winrt::resume_background();
        {
            auto const self = weak.lock();
            if (!self)
            {
                co_return;
            }
            // Ordered against the account binds, which take the same lock.
            std::scoped_lock const accountLock{ self->m_accountMutex };
            engine->AdoptAccount(from, to);
        }
        co_await uiContext;
        if (auto const self = weak.lock(); self && self->m_running)
        {
            self->RequestSynchronize();
        }
    }

    void DownloadService::RequestSynchronize()
    {
        auto const version = ++m_snapshotVersion;
        auto const accountVersion = m_accountVersion.load();
        auto const engine = m_engine;
        auto const weak = weak_from_this();
        concurrency::create_task([engine, weak, version, accountVersion]()
        {
            Snapshot snapshot;
            try
            {
                auto const self = weak.lock();
                if (!self)
                {
                    return;
                }
                std::scoped_lock const accountLock{ self->m_accountMutex };
                if (accountVersion != self->m_accountVersion.load()
                    || accountVersion != self->m_boundAccountVersion.load())
                {
                    return;
                }
                snapshot.Since = self->m_eventSequence.load();
                snapshot.Records = engine->List();
                snapshot.FreeBytes = engine->FreeBytes();
                snapshot.Directory = engine->DownloadDirectory();
            }
            catch (...)
            {
                return;
            }
            if (auto const self = weak.lock())
            {
                self->m_dispatcher.TryEnqueue([weak, snapshot = std::move(snapshot), version]() mutable
                {
                    if (auto const owner = weak.lock())
                    {
                        owner->ApplySnapshot(std::move(snapshot), version);
                    }
                });
            }
        });
    }

    // Only progress is applied from an event; every other change arrives with
    // the list read the event also requested. A record applied here stamps its
    // job, so a list read that began before the event cannot undo it.
    void DownloadService::ApplyEngineProgress(
        Downloads::DownloadRecord record,
        std::uint64_t accountVersion,
        std::uint64_t sequence)
    {
        if (!m_running || record.Status != DownloadStatus::Downloading
            || accountVersion != m_accountVersion.load()
            || accountVersion != m_boundAccountVersion.load())
        {
            return;
        }
        auto const owner = m_session->DataOwner();
        if (!owner || record.AccountKey != Downloads::MakeAccountKey(owner->Server, owner->User))
        {
            return;
        }
        auto const found = std::find_if(m_records.begin(), m_records.end(), [&record](auto const& candidate)
        {
            return candidate.JobId == record.JobId;
        });
        if (found != m_records.end() && found->UpdatedAt > record.UpdatedAt)
        {
            return;
        }
        auto& stamp = m_lastEventSequence[record.JobId];
        stamp = (std::max)(stamp, sequence);
        if (found == m_records.end())
        {
            m_records.push_back(std::move(record));
            std::stable_sort(m_records.begin(), m_records.end(), [](DownloadRecord const& left, DownloadRecord const& right)
            {
                return std::tie(left.CreatedAt, left.JobId) < std::tie(right.CreatedAt, right.JobId);
            });
        }
        else
        {
            *found = std::move(record);
        }
        RecordsChanged();
    }

    void DownloadService::ApplySnapshot(Snapshot snapshot, std::uint64_t version)
    {
        // Reads are requested in order but run on the thread pool, so a newer
        // request can read the engine before an older one. Only the newest read
        // of the engine applies: one that read before the last applied one
        // carries older state for every job.
        if (!m_running || version < m_snapshotFloor.load() || version <= m_appliedSnapshotVersion
            || snapshot.Since < m_appliedSince)
        {
            return;
        }
        m_appliedSnapshotVersion = version;
        m_appliedSince = snapshot.Since;
        m_records = DownloadsLogic::MergeListRead(
            std::move(snapshot.Records),
            m_records,
            m_lastEventSequence,
            snapshot.Since);
        // Every later read that applies began at or after this one, so a stamp
        // it already covers can never make a job newer again.
        std::erase_if(m_lastEventSequence, [since = snapshot.Since](auto const& entry)
        {
            return entry.second <= since;
        });
        m_freeBytes = snapshot.FreeBytes;
        m_directory = std::move(snapshot.Directory);
        RecordsChanged();
        EnrichMissingArtwork();
        EnrichMissingSidecars();
    }

    void DownloadService::EnrichMissingArtwork()
    {
        auto const accountVersion = m_accountVersion.load();
        auto const weak = weak_from_this();
        for (auto const& record : m_records)
        {
            auto const& media = record.Media;
            if (media.LandscapeArtwork
                || !media.MetaId || media.MetaId->empty()
                || media.MediaType.empty() || media.VideoId.empty()
                || !m_artworkPending.insert(record.JobId).second)
            {
                continue;
            }
            auto completion = m_artwork->ResolveAsync(
                winrt::hstring{ media.MediaType },
                winrt::hstring{ *media.MetaId },
                winrt::hstring{ media.VideoId }).then(
                [weak,
                 jobId = record.JobId,
                 accountKey = record.AccountKey,
                 accountVersion](concurrency::task<winrt::hstring> result)
                {
                    winrt::hstring artwork;
                    try { artwork = result.get(); } catch (...) {}
                    if (auto const owner = weak.lock())
                    {
                        if (!artwork.empty())
                        {
                            try
                            {
                                if (owner->m_running
                                    && accountVersion == owner->m_accountVersion.load()
                                    && accountVersion == owner->m_boundAccountVersion.load())
                                {
                                    static_cast<void>(owner->m_engine->SetLandscapeArtwork(
                                        jobId,
                                        accountKey,
                                        std::wstring{ artwork }));
                                }
                            }
                            catch (...)
                            {
                            }
                        }
                    }
                });
            static_cast<void>(completion);
        }
    }

    void DownloadService::EnrichMissingSidecars()
    {
        if (!m_sidecarFetcher)
        {
            m_sidecarFetcher = std::make_shared<DownloadSidecarFetcher>();
        }
        auto const accountVersion = m_accountVersion.load();
        for (auto const& record : m_records)
        {
            if (record.Status != DownloadStatus::Done || record.PendingDeletion)
            {
                continue;
            }
            auto const& media = record.Media;
            for (auto const kind : { Downloads::SidecarKind::Logo, Downloads::SidecarKind::Art })
            {
                std::wstring url;
                if (kind == Downloads::SidecarKind::Logo)
                {
                    // The same logo Stremio shows, by the title's IMDb id.
                    if (media.MetaId)
                    {
                        url = ::HaloDesktop::Controls::MetahubLogoUrl(*media.MetaId).value_or(L"");
                    }
                }
                else if (media.LandscapeArtwork)
                {
                    url = *media.LandscapeArtwork;
                }
                if (url.empty() || !IsSidecarSourceUrl(url))
                {
                    continue;
                }
                auto key = record.JobId + (kind == Downloads::SidecarKind::Logo ? L"|logo" : L"|art");
                if (!m_sidecarsTried.insert(std::move(key)).second)
                {
                    continue;
                }
                SaveSidecarAsync(
                    weak_from_this(),
                    m_engine,
                    m_sidecarFetcher,
                    record.JobId,
                    record.AccountKey,
                    kind,
                    std::move(url),
                    record.SidecarPath(kind),
                    accountVersion);
            }
        }
    }

    winrt::fire_and_forget DownloadService::SaveSidecarAsync(
        std::weak_ptr<DownloadService> weak,
        std::shared_ptr<Downloads::TransferEngine> engine,
        std::shared_ptr<DownloadSidecarFetcher> fetcher,
        std::wstring jobId,
        std::wstring accountKey,
        Downloads::SidecarKind kind,
        std::wstring url,
        std::filesystem::path target,
        std::uint64_t accountVersion)
    {
        try
        {
            co_await winrt::resume_background();
            std::error_code error;
            if (std::filesystem::exists(target, error) || error)
            {
                co_return;
            }
            auto const bytes = co_await fetcher->FetchAsync(std::move(url), kind);
            // The account may have changed while the picture came in; the
            // engine checks too, but nothing is written for a session that left.
            auto const owner = weak.lock();
            if (!owner || accountVersion != owner->m_accountVersion.load())
            {
                co_return;
            }
            static_cast<void>(engine->SaveSidecar(jobId, accountKey, kind, bytes));
        }
        catch (...)
        {
            // Offline, a missing picture or a refused write: the card keeps
            // showing the picture from the network, and the next launch tries again.
        }
    }

    void DownloadService::RecordsChanged()
    {
        UpdateThroughputTimer();
        NotifyChanged();
    }

    std::vector<DownloadsLogic::PausableRecord> DownloadService::PausableRecords() const
    {
        std::vector<DownloadsLogic::PausableRecord> result;
        result.reserve(m_records.size());
        for (auto const& record : m_records)
        {
            result.push_back(DownloadsLogic::PausableRecord{ .JobId = record.JobId, .Status = record.Status });
        }
        return result;
    }

    bool DownloadService::HasActiveTransfer() const noexcept
    {
        return std::any_of(m_records.begin(), m_records.end(), [](auto const& record)
        {
            return record.Status == DownloadStatus::Downloading;
        });
    }

    bool DownloadService::HasThroughputHistory() const
    {
        for (auto const value : m_throughput)
        {
            if (value > 0.0) return true;
        }
        return false;
    }

    // The chart is a clock, not an event log: one sample per bar regardless of
    // how many transfers are reporting, so the 28 slots always span one minute.
    // Stopping is left to the sampler, which keeps ticking until the trace has
    // drained; ending it here would freeze the strip mid-plateau.
    void DownloadService::UpdateThroughputTimer()
    {
        if (!m_throughputTimer)
        {
            return;
        }
        if (HasActiveTransfer() && !m_throughputTimer.IsRunning())
        {
            m_throughputTimer.Start();
        }
    }

    void DownloadService::SampleThroughput()
    {
        if (!m_running)
        {
            return;
        }
        std::uint64_t bytesPerSecond{};
        for (auto const& record : m_records)
        {
            if (record.Status == DownloadStatus::Downloading) bytesPerSecond += record.BytesPerSecond;
        }
        auto const megabytesPerSecond = static_cast<double>(bytesPerSecond) / (1024.0 * 1024.0);
        DownloadsLogic::PushThroughputSample(m_throughput, megabytesPerSecond);
        // Megabits, not mebibytes: the number is compared against what a line is
        // sold as, and the store drops anything that is not a new peak. Only these
        // periodic samples of the engine's windowed rate qualify; a raw
        // progress slice would record a burst as the line speed.
        m_devicePreferences->RecordMeasuredLineMbps(static_cast<double>(bytesPerSecond) * 8.0 / 1'000'000.0);
        // Sampling continues after the last transfer ends so the trace falls to
        // zero and the peak decays with it, rather than leaving a plateau and a
        // stale peak on screen for the rest of the session. Once the window
        // holds nothing but zeros there is nothing left to draw.
        if (m_throughputTimer && !HasActiveTransfer() && !HasThroughputHistory())
        {
            m_throughputTimer.Stop();
        }
        NotifyChanged();
    }

    void DownloadService::RunEngineAction(std::function<void()> action)
    {
        auto const weak = weak_from_this();
        auto const accountVersion = m_accountVersion.load();
        concurrency::create_task([weak, action = std::move(action), accountVersion]()
        {
            auto const self = weak.lock();
            if (!self || accountVersion != self->m_accountVersion.load())
            {
                return;
            }
            winrt::hstring errorMessage;
            try
            {
                action();
            }
            catch (std::exception const& error)
            {
                errorMessage = winrt::to_hstring(error.what());
            }
            catch (...)
            {
                errorMessage = L"The download action failed. Try again.";
            }
            if (auto const owner = weak.lock())
            {
                owner->m_dispatcher.TryEnqueue([weak, errorMessage, accountVersion]()
                {
                    if (auto const current = weak.lock();
                        current && accountVersion == current->m_accountVersion.load())
                    {
                        current->m_actionError = errorMessage;
                        current->RequestSynchronize();
                        current->NotifyChanged();
                    }
                });
            }
        });
    }

    void DownloadService::NotifyChanged()
    {
        auto const handlers = m_handlers;
        for (auto const& [token, handler] : handlers)
        {
            static_cast<void>(token);
            try { handler(); } catch (...) {}
        }
    }

    std::optional<Downloads::DownloadRecord> DownloadService::FindRecord(winrt::hstring const& id) const
    {
        auto const found = std::find_if(m_records.begin(), m_records.end(), [&id](auto const& record)
        {
            return record.JobId == id.c_str();
        });
        return found == m_records.end() ? std::nullopt : std::optional{ *found };
    }

    winrt::HaloDesktop::PlaybackRequest DownloadService::BuildPlaybackRequest(
        Downloads::DownloadRecord const& record) const
    {
        try
        {
            auto const files = m_engine->FilesForPlayback(record.JobId);
            auto const& media = record.Media;
            return winrt::make<winrt::HaloDesktop::implementation::PlaybackRequest>(
                winrt::hstring{ files.VideoPath.wstring() },
                true,
                winrt::hstring{ record.JobId },
                winrt::hstring{ record.SubtitleLanguage.value_or(L"") },
                winrt::hstring{ media.MediaType },
                winrt::hstring{ media.VideoId },
                winrt::hstring{ media.ItemId },
                winrt::hstring{ media.MetaId.value_or(L"") },
                winrt::hstring{ media.Title },
                winrt::hstring{ media.ShowName.value_or(L"") },
                winrt::hstring{ media.EpisodeLabel.value_or(L"") },
                winrt::hstring{ media.Poster.value_or(L"") },
                winrt::hstring{ media.AddonId.value_or(L"") },
                winrt::hstring{ media.BingeGroup.value_or(L"") },
                winrt::hstring{ record.FileName },
                record.TotalBytes,
                winrt::hstring{ media.VideoHash.value_or(L"") });
        }
        catch (...)
        {
            return nullptr;
        }
    }
}
