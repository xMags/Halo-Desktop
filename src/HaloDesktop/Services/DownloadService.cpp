#include "pch.h"
#include "Services/DownloadService.h"

#include "Models/Models.h"
#include "Services/DevicePreferencesStore.h"
#include "Services/DownloadSourceMatch.h"
#include "Services/StreamInfo.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace
{
    using HaloDesktop::Services::Downloads::DownloadFailureCode;
    using HaloDesktop::Services::Downloads::DownloadRecord;
    using HaloDesktop::Services::Downloads::DownloadStatus;

    winrt::hstring FormatBytes(std::uint64_t value)
    {
        constexpr std::array<wchar_t const*, 5> units{ L"B", L"KB", L"MB", L"GB", L"TB" };
        auto amount = static_cast<double>(value);
        std::size_t unit{};
        while (amount >= 1024.0 && unit + 1 < units.size())
        {
            amount /= 1024.0;
            ++unit;
        }
        std::wostringstream output;
        output << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << amount << L" " << units[unit];
        return winrt::hstring{ output.str() };
    }

    winrt::hstring FormatRate(std::uint64_t bytesPerSecond)
    {
        if (bytesPerSecond == 0)
        {
            return L"WAITING";
        }
        return FormatBytes(bytesPerSecond) + L"/s";
    }

    winrt::hstring Tag(DownloadRecord const& record)
    {
        if (record.Media.EpisodeLabel && !record.Media.EpisodeLabel->empty())
        {
            return winrt::hstring{ *record.Media.EpisodeLabel };
        }
        auto value = record.Media.MediaType;
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towupper(character));
        });
        return winrt::hstring{ value.empty() ? L"VIDEO" : value };
    }

    winrt::hstring Detail(DownloadRecord const& record)
    {
        if (record.Status == DownloadStatus::Failed)
        {
            return winrt::hstring{ HaloDesktop::Services::Downloads::FailureMessage(
                record.Failure.value_or(DownloadFailureCode::Unknown)) };
        }
        if (record.Status == DownloadStatus::Done)
        {
            return L"READY FOR OFFLINE PLAYBACK";
        }
        if (record.Status == DownloadStatus::Paused)
        {
            return L"PAUSED BY YOU";
        }
        if (record.Status == DownloadStatus::Queued)
        {
            return L"WAITING FOR THE CURRENT TRANSFER";
        }
        return FormatRate(record.BytesPerSecond);
    }

    winrt::HaloDesktop::DownloadState DisplayState(DownloadStatus status) noexcept
    {
        switch (status)
        {
        case DownloadStatus::Queued: return winrt::HaloDesktop::DownloadState::Queued;
        case DownloadStatus::Downloading: return winrt::HaloDesktop::DownloadState::Downloading;
        case DownloadStatus::Paused: return winrt::HaloDesktop::DownloadState::Paused;
        case DownloadStatus::Done: return winrt::HaloDesktop::DownloadState::OnDisk;
        case DownloadStatus::Failed: return winrt::HaloDesktop::DownloadState::Failed;
        }
        return winrt::HaloDesktop::DownloadState::Failed;
    }

    winrt::HaloDesktop::DownloadItem DisplayItem(DownloadRecord const& record)
    {
        HaloDesktop::Api::Dto::StreamRecord stream;
        stream.Name = record.Media.StreamName ? std::optional<winrt::hstring>{ winrt::hstring{ *record.Media.StreamName } } : std::nullopt;
        stream.Title = record.Media.StreamTitle ? std::optional<winrt::hstring>{ winrt::hstring{ *record.Media.StreamTitle } } : std::nullopt;
        stream.Filename = winrt::hstring{ record.FileName };
        stream.VideoSize = record.TotalBytes > 0 ? std::optional<std::uint64_t>{ record.TotalBytes } : record.Media.VideoSize;
        auto const info = HaloDesktop::Services::ParseStreamInfo(stream);
        auto const total = record.TotalBytes > 0 ? record.TotalBytes : record.Media.VideoSize.value_or(0);
        auto const progress = record.Status == DownloadStatus::Done
            ? 1.0
            : (total > 0 ? static_cast<double>(record.DownloadedBytes) / static_cast<double>(total) : 0.0);
        auto const name = record.Media.ShowName.value_or(record.Media.Title);
        auto const sub = record.Media.ShowName ? record.Media.Title : record.FileName;
        auto const subtitle = record.SubtitleLanguage
            ? winrt::hstring{ L"Subtitle: " + *record.SubtitleLanguage }
            : winrt::hstring{ L"No subtitle sidecar" };
        auto const requiresNewSource = record.Failure
            && HaloDesktop::Services::Downloads::RequiresNewSource(*record.Failure);
        return winrt::make<winrt::HaloDesktop::implementation::DownloadItem>(
            winrt::hstring{ record.JobId },
            Tag(record),
            winrt::hstring{ name },
            winrt::hstring{ sub },
            DisplayState(record.Status),
            progress,
            Detail(record),
            info.Quality.value_or(L"UNKNOWN"),
            info.Codec.value_or(L"UNKNOWN"),
            FormatBytes(total),
            subtitle,
            winrt::hstring{ record.Media.VideoId },
            winrt::hstring{ record.Media.Poster.value_or(L"") },
            requiresNewSource);
    }

}

namespace HaloDesktop::Services
{
    DownloadService::DownloadService(
        std::shared_ptr<Downloads::TransferEngine> engine,
        std::shared_ptr<ISessionService> session,
        std::shared_ptr<DevicePreferencesStore> devicePreferences,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_engine(std::move(engine)),
          m_session(std::move(session)),
          m_devicePreferences(std::move(devicePreferences)),
          m_dispatcher(std::move(dispatcher)),
          m_transfers(winrt::single_threaded_observable_vector<winrt::HaloDesktop::DownloadItem>()),
          m_ready(winrt::single_threaded_observable_vector<winrt::HaloDesktop::DownloadItem>()),
          m_throughput(winrt::single_threaded_observable_vector<double>())
    {
        if (!m_engine || !m_session || !m_devicePreferences || !m_dispatcher)
        {
            throw std::invalid_argument{ "DownloadService requires all dependencies." };
        }
        for (std::size_t index = 0; index < 30; ++index)
        {
            m_throughput.Append(0.0);
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
                self->m_dispatcher.TryEnqueue([weak, record, accountVersion]() mutable
                {
                    if (auto const owner = weak.lock())
                    {
                        owner->ApplyEngineProgress(std::move(record), accountVersion);
                    }
                });
                self->RequestSynchronize();
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
    }

    void DownloadService::RebindAccount()
    {
        auto const server = std::wstring{ m_session->ServerUrl().c_str() };
        auto const userId = std::wstring{ m_session->UserId().c_str() };
        auto const accountVersion = ++m_accountVersion;
        auto const version = ++m_snapshotVersion;
        m_snapshotFloor.store(version);
        m_records.clear();
        m_pauseAllIds.clear();
        m_pausedAll = false;
        m_freeBytes.reset();
        m_directory.clear();
        m_actionError.clear();
        m_throughput.Clear();
        RebuildObservables();
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
                }
                if (accountVersion != self->m_accountVersion.load())
                {
                    engine->ClearAccount();
                    return;
                }
                self->m_boundAccountVersion.store(accountVersion);
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

    void DownloadService::PauseAll()
    {
        if (m_pausedAll)
        {
            return;
        }
        m_pauseAllIds.clear();
        for (auto const& record : m_records)
        {
            if (Downloads::IsActive(record.Status))
            {
                m_pauseAllIds.insert(record.JobId);
            }
        }
        m_pausedAll = true;
        auto ids = m_pauseAllIds;
        RunEngineAction([engine = m_engine, ids = std::move(ids)]()
        {
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
        if (!m_pausedAll)
        {
            return;
        }
        auto ids = std::move(m_pauseAllIds);
        m_pauseAllIds.clear();
        m_pausedAll = false;
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
        RunEngineAction([engine = m_engine, jobId = std::wstring{ id.c_str() }]() { engine->Remove(jobId); });
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

    concurrency::task<DownloadStartOutcome> DownloadService::StartDownloadAsync(
        Downloads::DownloadStartRequest request)
    {
        auto const server = std::wstring{ m_session->ServerUrl().c_str() };
        auto const userId = std::wstring{ m_session->UserId().c_str() };
        auto const accountVersion = m_accountVersion.load();
        if (server.empty() || userId.empty())
        {
            co_return DownloadStartOutcome::Failed;
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
                co_return DownloadStartOutcome::Failed;
            }
            m_engine->SetAccount(server, userId);
            if (accountVersion != m_accountVersion.load())
            {
                m_engine->ClearAccount();
                co_return DownloadStartOutcome::Failed;
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
                co_return DownloadStartOutcome::AlreadyExists;
            }
            if (existing != before.end()
                && !replacing
                && (existing->SourceFingerprint != fingerprint
                    || (existing->Failure && Downloads::RequiresNewSource(*existing->Failure))))
            {
                co_return DownloadStartOutcome::ReplacementRequired;
            }
            static_cast<void>(m_engine->Start(std::move(request)));
            if (accountVersion != m_accountVersion.load())
            {
                m_engine->ClearAccount();
                co_return DownloadStartOutcome::Failed;
            }
            RequestSynchronize();
            co_return DownloadStartOutcome::Started;
        }
        catch (...)
        {
            RequestSynchronize();
            co_return DownloadStartOutcome::Failed;
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
        return winrt::make<winrt::HaloDesktop::implementation::SourcesNavParams>(
            winrt::hstring{ media.MediaType },
            winrt::hstring{ media.MetaId.value_or(L"") },
            winrt::hstring{ media.VideoId },
            winrt::hstring{ media.ItemId },
            winrt::hstring{ media.Title },
            winrt::hstring{ media.ShowName.value_or(L"") },
            winrt::hstring{ media.EpisodeLabel.value_or(L"") },
            winrt::hstring{ media.Poster.value_or(L"") });
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
    bool DownloadService::IsPausedAll() const noexcept { return m_pausedAll; }
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

    std::int32_t DownloadService::ActiveCount() const noexcept
    {
        return static_cast<std::int32_t>(std::count_if(m_records.begin(), m_records.end(), [](auto const& record)
        {
            return Downloads::IsActive(record.Status);
        }));
    }
    double DownloadService::AggregateRate() const noexcept { return m_aggregateRate; }
    winrt::hstring DownloadService::QueueLine() const
    {
        auto const active = ActiveCount();
        if (active == 0)
        {
            return L"No active transfers";
        }
        std::wostringstream output;
        output << active << (active == 1 ? L" active transfer" : L" active transfers");
        return winrt::hstring{ output.str() };
    }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> DownloadService::Transfers() const { return m_transfers; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::DownloadItem> DownloadService::Ready() const { return m_ready; }
    winrt::Windows::Foundation::Collections::IObservableVector<double> DownloadService::Throughput() const { return m_throughput; }
    std::uint64_t DownloadService::StoredBytes() const noexcept { return m_storedBytes; }
    std::uint64_t DownloadService::InFlightBytes() const noexcept { return m_inFlightBytes; }
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

    void DownloadService::ApplyEngineProgress(Downloads::DownloadRecord record, std::uint64_t accountVersion)
    {
        if (!m_running || record.Status != DownloadStatus::Downloading
            || accountVersion != m_accountVersion.load()
            || accountVersion != m_boundAccountVersion.load())
        {
            return;
        }
        auto const userId = std::wstring{ m_session->UserId().c_str() };
        if (userId.empty()
            || record.AccountKey != Downloads::MakeAccountKey(
                std::wstring{ m_session->ServerUrl().c_str() }, userId))
        {
            return;
        }
        auto const found = std::find_if(m_records.begin(), m_records.end(), [&record](auto const& candidate)
        {
            return candidate.JobId == record.JobId;
        });
        if (found == m_records.end())
        {
            m_records.push_back(std::move(record));
        }
        else if (found->UpdatedAt <= record.UpdatedAt)
        {
            *found = std::move(record);
        }
        RebuildObservables();
    }

    void DownloadService::ApplySnapshot(Snapshot snapshot, std::uint64_t version)
    {
        if (!m_running || version < m_snapshotFloor.load() || version <= m_appliedSnapshotVersion)
        {
            return;
        }
        m_appliedSnapshotVersion = version;
        for (auto& record : snapshot.Records)
        {
            auto const current = std::find_if(m_records.begin(), m_records.end(), [&record](auto const& candidate)
            {
                return candidate.JobId == record.JobId;
            });
            if (current != m_records.end() && current->UpdatedAt > record.UpdatedAt)
            {
                record = *current;
            }
        }
        m_records = std::move(snapshot.Records);
        m_freeBytes = snapshot.FreeBytes;
        m_directory = std::move(snapshot.Directory);
        RebuildObservables();
    }

    void DownloadService::RebuildObservables()
    {
        m_storedBytes = 0;
        m_inFlightBytes = 0;
        m_aggregateRate = 0.0;
        std::uint64_t bytesPerSecond{};
        std::vector<winrt::HaloDesktop::DownloadItem> transfers;
        std::vector<winrt::HaloDesktop::DownloadItem> ready;
        for (auto const& record : m_records)
        {
            bytesPerSecond += record.BytesPerSecond;
            m_aggregateRate += static_cast<double>(record.BytesPerSecond) / (1024.0 * 1024.0);
            if (record.Status == DownloadStatus::Done)
            {
                m_storedBytes += record.TotalBytes > 0 ? record.TotalBytes : record.DownloadedBytes;
                ready.push_back(DisplayItem(record));
            }
            else
            {
                m_inFlightBytes += record.DownloadedBytes;
                transfers.push_back(DisplayItem(record));
            }
        }
        m_transfers.Clear();
        for (auto const& item : transfers) m_transfers.Append(item);
        m_ready.Clear();
        for (auto const& item : ready) m_ready.Append(item);
        if (m_throughput.Size() >= 30) m_throughput.RemoveAt(0);
        m_throughput.Append(m_aggregateRate);
        // Megabits, not mebibytes: the number is compared against what a line is
        // sold as, and the store drops anything that is not a new peak.
        m_devicePreferences->RecordMeasuredLineMbps(static_cast<double>(bytesPerSecond) * 8.0 / 1'000'000.0);
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
                winrt::hstring{ media.VideoHash.value_or(L"") },
                L"OFFLINE");
        }
        catch (...)
        {
            return nullptr;
        }
    }
}
