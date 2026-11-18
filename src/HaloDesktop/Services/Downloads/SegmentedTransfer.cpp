#include "pch.h"
#include "Services/Downloads/SegmentedTransfer.h"

#include "Services/Downloads/DownloadHttp.h"
#include "Services/Downloads/TransferRateWindow.h"
#include "Storage/FileStorage.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
#include <wil/resource.h>
#include <winioctl.h>

namespace
{
    namespace Downloads = HaloDesktop::Services::Downloads;
    namespace Segments = HaloDesktop::Services::Downloads::Segments;
    namespace Http = HaloDesktop::Services::Downloads::Http;
    using Clock = Segments::Clock;
    using Downloads::DownloadFailureCode;
    using Downloads::TransferFailure;
    using Downloads::TransferFailureKind;

    constexpr std::size_t ChunkBytes = 64u * 1024u;
    constexpr std::uint64_t SpaceCheckBytes = 8ull * 1024ull * 1024ull;
    // A sidecar of the largest table this app accepts is far below this.
    constexpr std::uint64_t MaximumTableBytes = 1024ull * 1024ull;

    [[nodiscard]] TransferFailure Permanent(DownloadFailureCode code) noexcept
    {
        return TransferFailure{ .Kind = TransferFailureKind::Permanent, .Code = code };
    }

    [[nodiscard]] TransferFailure Retryable(DownloadFailureCode code) noexcept
    {
        return TransferFailure{ .Kind = TransferFailureKind::Retryable, .Code = code };
    }

    [[nodiscard]] TransferFailure StorageFull() noexcept
    {
        return Permanent(DownloadFailureCode::StorageFull);
    }

    [[nodiscard]] std::optional<TransferFailure> ClassifyStatus(std::uint32_t status) noexcept
    {
        if (status == 200 || status == 206)
        {
            return std::nullopt;
        }
        if (status == 401 || status == 403)
        {
            return Permanent(DownloadFailureCode::SourceExpired);
        }
        if (status == 416)
        {
            return Permanent(DownloadFailureCode::InvalidRange);
        }
        if (status == 408 || status == 425 || status == 429 || (status >= 500 && status <= 599))
        {
            return Retryable(DownloadFailureCode::ServerUnavailable);
        }
        return Permanent(DownloadFailureCode::SourceRejected);
    }

    [[nodiscard]] std::optional<std::uint64_t> AvailableBytes(std::filesystem::path const& path) noexcept
    {
        ULARGE_INTEGER available{};
        if (!GetDiskFreeSpaceExW(path.c_str(), &available, nullptr, nullptr))
        {
            return std::nullopt;
        }
        return available.QuadPart;
    }

    // True when the file is gone afterwards, whether or not it was there.
    [[nodiscard]] bool RemoveIfPresent(std::filesystem::path const& path) noexcept
    {
        if (DeleteFileW(path.c_str()))
        {
            return true;
        }
        auto const error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }

    enum class TableRead
    {
        Found,
        NotFound,
        Unreadable,
    };

    [[nodiscard]] TableRead ReadTableFile(std::filesystem::path const& path, std::string& raw) noexcept
    {
        try
        {
            wil::unique_hfile file{ CreateFileW(
                path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr) };
            if (!file)
            {
                auto const error = GetLastError();
                return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                    ? TableRead::NotFound
                    : TableRead::Unreadable;
            }
            LARGE_INTEGER size{};
            if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0
                || static_cast<std::uint64_t>(size.QuadPart) > MaximumTableBytes)
            {
                return TableRead::Unreadable;
            }
            raw.assign(static_cast<std::size_t>(size.QuadPart), '\0');
            std::size_t offset{};
            while (offset < raw.size())
            {
                DWORD read{};
                if (!ReadFile(file.get(), raw.data() + offset, static_cast<DWORD>(raw.size() - offset), &read, nullptr)
                    || read == 0)
                {
                    return TableRead::Unreadable;
                }
                offset += read;
            }
            return TableRead::Found;
        }
        catch (...)
        {
            return TableRead::Unreadable;
        }
    }

    [[nodiscard]] std::uint64_t PartLength(std::filesystem::path const& part) noexcept
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(part.c_str(), GetFileExInfoStandard, &data)
            || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            return 0;
        }
        return (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    }

    enum class EventType
    {
        Opened,
        OpenFailed,
        Chunk,
        End,
        ReadFailed,
    };

    struct ConnectionEvent final
    {
        EventType Type{ EventType::ReadFailed };
        std::uint64_t Id{};
        Http::ResponseHead Head;
        TransferFailure Failure;
        std::vector<std::uint8_t> Data;
    };

    // What the connection workers tell the coordinator. The only state the two
    // sides share; its lock is held only to move an event in or out.
    class EventQueue final
    {
    public:
        void Push(ConnectionEvent event)
        {
            {
                std::scoped_lock const lock{ m_mutex };
                m_events.push_back(std::move(event));
            }
            m_ready.notify_one();
        }

        [[nodiscard]] std::optional<ConnectionEvent> WaitFor(Clock::duration timeout)
        {
            std::unique_lock lock{ m_mutex };
            if (!m_ready.wait_for(lock, timeout, [this] { return !m_events.empty(); }))
            {
                return std::nullopt;
            }
            auto event = std::move(m_events.front());
            m_events.pop_front();
            return event;
        }

    private:
        std::mutex m_mutex;
        std::condition_variable m_ready;
        std::deque<ConnectionEvent> m_events;
    };

    struct OpenCommand final
    {
        std::wstring Url;
        Http::HeaderMap Headers;
        std::optional<std::uint64_t> Start;
        std::optional<std::wstring> Validator;
    };

    // One connection's thread. It holds the connection's WinHTTP handles and does
    // one blocking call per command; the coordinator never touches the handles.
    // Abort closes whatever call is in flight. The destructor aborts and joins.
    class ConnectionWorker final
    {
    public:
        ConnectionWorker(std::uint64_t id, EventQueue& events)
            : m_id(id),
              m_events(events),
              m_thread([this](std::stop_token stop) { Run(stop); })
        {
        }

        ~ConnectionWorker()
        {
            Abort();
            m_thread.request_stop();
            m_wake.notify_all();
            // m_thread is the last member, so it is joined before anything it uses.
        }

        ConnectionWorker(ConnectionWorker const&) = delete;
        ConnectionWorker& operator=(ConnectionWorker const&) = delete;

        void Open(OpenCommand command)
        {
            Post(Command{ .Kind = CommandKind::Open, .Open = std::move(command) });
        }

        void Read()
        {
            Post(Command{ .Kind = CommandKind::Read });
        }

        void Abort() noexcept
        {
            m_cancellation.Cancel();
        }

        // No command is running or waiting, so destroying it joins at once.
        [[nodiscard]] bool Idle() const noexcept
        {
            return !m_busy.load(std::memory_order_acquire);
        }

    private:
        enum class CommandKind
        {
            Open,
            Read,
        };

        struct Command final
        {
            CommandKind Kind{ CommandKind::Read };
            OpenCommand Open;
        };

        void Post(Command command)
        {
            {
                std::scoped_lock const lock{ m_mutex };
                m_busy.store(true, std::memory_order_release);
                m_command = std::move(command);
            }
            m_wake.notify_all();
        }

        void Run(std::stop_token stop)
        {
            for (;;)
            {
                Command command;
                {
                    std::unique_lock lock{ m_mutex };
                    if (!m_wake.wait(lock, stop, [this] { return m_command.has_value(); }))
                    {
                        return;
                    }
                    command = std::move(*m_command);
                    m_command.reset();
                }
                auto event = command.Kind == CommandKind::Open
                    ? ExecuteOpen(command.Open)
                    : ExecuteRead();
                event.Id = m_id;
                // Cleared before the event is posted: the coordinator sends the
                // next command only after it has the event, so a later command's
                // busy mark can never be overwritten here.
                m_busy.store(false, std::memory_order_release);
                try
                {
                    m_events.Push(std::move(event));
                }
                catch (...)
                {
                    // Out of memory: the coordinator sees nothing from this
                    // connection, and its stall check ends it.
                }
            }
        }

        [[nodiscard]] ConnectionEvent ExecuteOpen(OpenCommand const& command) noexcept
        {
            ConnectionEvent event;
            event.Type = EventType::OpenFailed;
            event.Failure = Retryable(DownloadFailureCode::Network);
            try
            {
                m_response = Http::OpenGet(command.Url, command.Headers, command.Start, command.Validator, m_cancellation);
                event.Type = EventType::Opened;
                event.Head = m_response->Head;
            }
            catch (Http::RedirectRejected const&)
            {
                event.Failure = Permanent(DownloadFailureCode::SourceRejected);
            }
            catch (...)
            {
            }
            return event;
        }

        [[nodiscard]] ConnectionEvent ExecuteRead() noexcept
        {
            ConnectionEvent event;
            event.Type = EventType::ReadFailed;
            if (!m_response)
            {
                return event;
            }
            std::size_t read{};
            switch (Http::ReadSome(*m_response, m_buffer, read, m_cancellation))
            {
            case Http::ReadOutcome::Data:
                try
                {
                    event.Data.assign(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(read));
                    event.Type = EventType::Chunk;
                }
                catch (...)
                {
                }
                break;
            case Http::ReadOutcome::End:
                event.Type = EventType::End;
                break;
            case Http::ReadOutcome::Failed:
                break;
            }
            return event;
        }

        std::uint64_t m_id;
        EventQueue& m_events;
        Http::Cancellation m_cancellation;
        std::mutex m_mutex;
        std::condition_variable_any m_wake;
        std::optional<Command> m_command;
        std::atomic_bool m_busy{};
        // Only the worker thread touches these while it runs.
        std::optional<Http::Response> m_response;
        std::array<std::uint8_t, ChunkBytes> m_buffer{};
        std::jthread m_thread;
    };

    enum class ResumeKind
    {
        Fresh,
        // A part written front to back, by one connection or by a build from
        // before segment tables.
        Contiguous,
        Table,
        // Every byte arrived; only the final rename is missing.
        Complete,
    };

    struct ResumePlan final
    {
        ResumeKind Kind{ ResumeKind::Fresh };
        std::uint64_t Done{};
        std::optional<Segments::SegmentTable> Table;
    };

    struct FirstResponse final
    {
        std::unique_ptr<ConnectionWorker> Worker;
        std::uint64_t Id{};
        // Where the body starts when it continues earlier progress.
        std::optional<std::uint64_t> Start;
        std::uint64_t Total{};
        // The source stated the size, rather than the addon guessing it.
        bool TotalStated{};
        std::optional<std::wstring> ResponseValidator;
        // The If-Range value for every later range request.
        std::optional<std::wstring> Validator;
        bool RangesProven{};
        bool RangesRefused{};
    };

    enum class ConnectionState
    {
        Opening,
        Reading,
        // Reached its segment's end before the source proved it serves ranges.
        // It holds what it read past the end and waits: if the next segment's
        // connection is refused, it takes that range back and reads on.
        Parked,
    };

    struct Connection final
    {
        std::uint32_t Segment{};
        std::uint64_t StartedAt{};
        // Opened before the source proved it serves ranges.
        bool Probe{};
        ConnectionState State{ ConnectionState::Opening };
        // Bytes read past the segment's end, for the overlap check.
        std::vector<std::uint8_t> Overlap;
        // What a parked connection read past its end.
        std::vector<std::uint8_t> Held;
        Clock::time_point LastActivity;
        Clock::time_point OpenedAt;
        std::unique_ptr<ConnectionWorker> Worker;
    };

    enum class FlowKind
    {
        Continue,
        Park,
        Stop,
    };

    struct Flow final
    {
        FlowKind Kind{ FlowKind::Continue };
        std::vector<std::uint8_t> Held;
    };

    class Coordinator final
    {
    public:
        Coordinator(
            Downloads::SegmentedJob const& job,
            std::atomic_bool const& cancel,
            std::stop_token stop,
            Downloads::SegmentedHost const& host,
            Downloads::SegmentedOptions const& options)
            : m_job(job),
              m_cancel(cancel),
              m_stop(std::move(stop)),
              m_host(host),
              m_options(options),
              m_tablePath(Segments::TablePath(job.Part)),
              m_rate(options.Timeouts.RateWindow)
        {
        }

        Coordinator(Coordinator const&) = delete;
        Coordinator& operator=(Coordinator const&) = delete;

        ~Coordinator()
        {
            for (auto& [id, connection] : m_connections)
            {
                static_cast<void>(id);
                if (connection.Worker) connection.Worker->Abort();
            }
            for (auto& worker : m_retired)
            {
                worker->Abort();
            }
            // Members then destroy the workers, joining every thread.
        }

        [[nodiscard]] Downloads::SegmentedFinished Execute()
        {
            auto resume = LoadResume();
            if (resume.Kind == ResumeKind::Complete)
            {
                return FinishExisting();
            }
            std::optional<std::uint64_t> firstStart;
            if (resume.Kind == ResumeKind::Contiguous)
            {
                firstStart = resume.Done;
            }
            else if (resume.Kind == ResumeKind::Table)
            {
                // The first connection resumes what the planner would hand any
                // free connection: the segment with the most left.
                auto const step = resume.Table->Choose(Clock::now(), (std::numeric_limits<std::uint64_t>::max)());
                if (!step || step->Kind != Segments::PlanStepKind::Resume)
                {
                    throw Permanent(DownloadFailureCode::InvalidRange);
                }
                firstStart = resume.Table->Segments()[step->Index].Position;
            }

            auto first = OpenFirst(firstStart);
            // A whole-file answer to a resume starts the download again.
            std::optional<Segments::SegmentTable> table;
            if (first.Start)
            {
                table = std::move(resume.Table);
            }
            if (m_host.ResponseMetadata)
            {
                try
                {
                    m_host.ResponseMetadata(first.ResponseValidator, first.Total);
                }
                catch (...)
                {
                    throw Permanent(DownloadFailureCode::Unknown);
                }
            }

            std::error_code directoryError;
            std::filesystem::create_directories(m_job.Root, directoryError);
            if (directoryError)
            {
                throw StorageFull();
            }
            auto const resumed = table.has_value() || first.Start.has_value();
            m_file.reset(CreateFileW(
                m_job.Part.c_str(),
                GENERIC_WRITE | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_DELETE,
                nullptr,
                resumed ? OPEN_EXISTING : CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr));
            if (!m_file)
            {
                throw StorageFull();
            }
            // Only after the part is empty: a full-length part without its table
            // would pass for a finished download.
            if (!resumed && !RemoveIfPresent(m_tablePath))
            {
                throw StorageFull();
            }

            auto const segmented = table.has_value();
            auto const start = first.Start.value_or(0);
            if (!table)
            {
                auto const end = first.TotalStated && first.Total > 0 ? first.Total : Segments::Unbounded;
                table = Segments::SegmentTable::Contiguous(start, end);
            }
            m_table = std::move(table);
            std::optional<std::uint32_t> firstKey;
            for (auto const& segment : m_table->Segments())
            {
                if (segment.Position == start && !segment.IsComplete())
                {
                    firstKey = segment.Key;
                    break;
                }
            }
            if (!firstKey)
            {
                throw Permanent(DownloadFailureCode::InvalidRange);
            }

            auto const now = Clock::now();
            m_total = first.Total;
            m_responseValidator = first.ResponseValidator;
            m_validator = first.Validator;
            m_multiAllowed = first.TotalStated
                && first.Total > 0
                && first.Validator.has_value()
                && !first.RangesRefused
                && m_options.Tuning.MaxConnections > 1;
            m_rangesProven = first.RangesProven;
            m_budget.emplace(m_options.Tuning.MaxConnections, m_options.Tuning.RampInterval, now);
            m_rate.Reset(now, m_table->Received());
            m_lastProgress = now;
            m_lastDurable = now;
            m_lastSpaceCheck = now;

            try
            {
                if (segmented)
                {
                    m_tableStarted = true;
                    SizeFile();
                }
                CheckInitialSpace();
                m_table->Begin(*firstKey, first.Id);
                Connection connection;
                connection.Segment = *firstKey;
                connection.StartedAt = start;
                connection.State = ConnectionState::Opening;
                connection.LastActivity = now;
                connection.OpenedAt = now;
                connection.Worker = std::move(first.Worker);
                m_connections.emplace(first.Id, std::move(connection));
                Arm(first.Id);

                Drive();
                ThrowIfStopped();
            }
            catch (...)
            {
                Abandon();
                throw;
            }
            return Finish();
        }

    private:
        [[nodiscard]] bool Cancelled() const noexcept
        {
            return m_cancel.load(std::memory_order_acquire) || m_stop.stop_requested();
        }

        void ThrowIfStopped() const
        {
            if (m_stop.stop_requested())
            {
                throw TransferFailure{ .Kind = TransferFailureKind::Shutdown };
            }
            if (m_cancel.load(std::memory_order_acquire))
            {
                throw TransferFailure{ .Kind = TransferFailureKind::Paused };
            }
        }

        // A sidecar decides the resume whenever it exists, readable or not: a part
        // that once had one may be full length with holes, so its length says
        // nothing. Without one, the part's length is what arrived.
        [[nodiscard]] ResumePlan LoadResume() const
        {
            auto const partLength = PartLength(m_job.Part);
            std::string raw;
            switch (ReadTableFile(m_tablePath, raw))
            {
            case TableRead::Found:
            {
                std::optional<Segments::SegmentTable> table;
                if (auto const saved = Segments::Decode(raw))
                {
                    auto const validator = m_job.Validator
                        ? std::optional<std::wstring_view>{ *m_job.Validator }
                        : std::nullopt;
                    table = Segments::SegmentTable::Restore(*saved, m_job.KnownTotal, validator, partLength);
                }
                if (!table)
                {
                    return ResumePlan{};
                }
                if (table->IsComplete())
                {
                    return ResumePlan{ .Kind = ResumeKind::Complete };
                }
                return ResumePlan{ .Kind = ResumeKind::Table, .Table = std::move(table) };
            }
            case TableRead::Unreadable:
                return ResumePlan{};
            case TableRead::NotFound:
                break;
            }
            if (m_job.KnownTotal > 0 && partLength > m_job.KnownTotal)
            {
                throw Permanent(DownloadFailureCode::InvalidRange);
            }
            if (partLength > 0 && partLength == m_job.KnownTotal)
            {
                return ResumePlan{ .Kind = ResumeKind::Complete };
            }
            if (partLength > 0 && !m_job.Validator)
            {
                // Without ETag or Last-Modified there is no safe If-Range value.
                // Restarting loses bytes; appending could silently corrupt a film.
                return ResumePlan{};
            }
            if (partLength > 0)
            {
                return ResumePlan{ .Kind = ResumeKind::Contiguous, .Done = partLength };
            }
            return ResumePlan{};
        }

        // Every byte arrived in an earlier run, which stopped before the rename.
        [[nodiscard]] Downloads::SegmentedFinished FinishExisting() const
        {
            if (!RemoveIfPresent(m_tablePath))
            {
                throw StorageFull();
            }
            return Downloads::SegmentedFinished{
                .Bytes = m_job.KnownTotal,
                .Total = m_job.KnownTotal,
                .Validator = m_job.Validator,
            };
        }

        [[nodiscard]] std::pair<std::unique_ptr<ConnectionWorker>, std::uint64_t> NewWorker()
        {
            auto const id = m_nextConnection++;
            return { std::make_unique<ConnectionWorker>(id, m_events), id };
        }

        // Waits for the first request's answer, polling the pause and shutdown
        // signals: a request never looks at them itself, so without this a pause
        // would wait for a slow source to start answering.
        [[nodiscard]] Http::ResponseHead AwaitFirstOpen(
            ConnectionWorker& worker,
            std::uint64_t id,
            std::optional<std::uint64_t> start,
            std::optional<std::wstring> const& validator)
        {
            worker.Open(OpenCommand{
                .Url = m_job.Url,
                .Headers = m_job.Headers,
                .Start = start,
                .Validator = start ? validator : std::nullopt,
            });
            auto const deadline = Clock::now() + m_options.Timeouts.FirstResponse;
            for (;;)
            {
                if (Cancelled())
                {
                    worker.Abort();
                    ThrowIfStopped();
                }
                auto const now = Clock::now();
                if (now >= deadline)
                {
                    worker.Abort();
                    throw Retryable(DownloadFailureCode::Network);
                }
                auto const wait = (std::min)(
                    std::chrono::duration_cast<Clock::duration>(m_options.Timeouts.CancelPoll),
                    deadline - now);
                auto event = m_events.WaitFor(wait);
                if (!event || event->Id != id)
                {
                    continue;
                }
                if (event->Type == EventType::OpenFailed)
                {
                    throw event->Failure;
                }
                if (event->Type == EventType::Opened)
                {
                    return std::move(event->Head);
                }
            }
        }

        // The engine's one request, as it always was: a resume asks for the rest
        // with If-Range, a whole-file answer restarts, and a ranged answer from
        // another origin is trusted only when it names the saved validator.
        [[nodiscard]] FirstResponse OpenFirst(std::optional<std::uint64_t> start)
        {
            auto [worker, id] = NewWorker();
            auto head = AwaitFirstOpen(*worker, id, start, m_job.Validator);
            std::optional<Downloads::ContentRange> contentRange;
            std::optional<std::wstring> responseValidator;
            for (;;)
            {
                if (auto const failure = ClassifyStatus(head.Status))
                {
                    throw *failure;
                }
                if (start && head.Status == 200)
                {
                    start.reset();
                }
                contentRange.reset();
                if (head.Status == 206)
                {
                    auto const range = head.ContentRange
                        ? Downloads::ParseContentRange(*head.ContentRange)
                        : std::nullopt;
                    if (!range)
                    {
                        throw Permanent(DownloadFailureCode::InvalidRange);
                    }
                    auto const lengthMatches = !head.ContentLength
                        || *head.ContentLength == range->End - range->Start + 1;
                    if (range->Start != start.value_or(0)
                        || (m_job.KnownTotal > 0 && range->Total != m_job.KnownTotal)
                        || !lengthMatches)
                    {
                        throw Permanent(DownloadFailureCode::InvalidRange);
                    }
                    contentRange = range;
                }
                responseValidator = Http::ValidatorOf(head);
                if (start && m_job.Validator && responseValidator && *m_job.Validator != *responseValidator)
                {
                    throw Permanent(DownloadFailureCode::InvalidRange);
                }
                // Resolver links redirect to the debrid host's own origin, so a
                // resume of them always crosses one. The If-Range guard went
                // along with the range: a 206 that names the validator this
                // download was saved under, at the size it was saved at (checked
                // above), is the same object, wherever it came from.
                auto const confirmed = m_job.Validator.has_value() && responseValidator == m_job.Validator;
                if (start && head.Status == 206 && head.CrossedOrigin && !confirmed)
                {
                    // It may describe a different object. Take the whole file from
                    // the original URL instead, which strips protected headers at
                    // the same hop again.
                    worker->Abort();
                    m_retired.push_back(std::move(worker));
                    std::tie(worker, id) = NewWorker();
                    head = AwaitFirstOpen(*worker, id, std::nullopt, std::nullopt);
                    start.reset();
                    continue;
                }
                break;
            }
            auto const encoded = head.ContentEncoding && *head.ContentEncoding != L"identity";
            auto const noRanges = head.AcceptRanges
                && CompareStringOrdinal(head.AcceptRanges->c_str(), -1, L"none", -1, TRUE) == CSTR_EQUAL;
            FirstResponse result;
            result.RangesRefused = encoded || noRanges;
            if (contentRange)
            {
                result.Total = contentRange->Total;
                result.TotalStated = true;
            }
            else if (head.ContentLength)
            {
                result.Total = *head.ContentLength;
                result.TotalStated = true;
            }
            else
            {
                result.Total = m_job.SizeHint.value_or(0);
            }
            // Refused before a byte is kept: taken as the film, the stand-in
            // would finish as a complete download that plays a few seconds.
            if (result.TotalStated && Downloads::IsStandInForVideo(result.Total, m_job.SizeHint))
            {
                throw Permanent(DownloadFailureCode::SourceNotReady);
            }
            auto validator = responseValidator;
            if (!validator && start)
            {
                validator = m_job.Validator;
            }
            if (validator && validator->empty())
            {
                validator.reset();
            }
            result.Validator = std::move(validator);
            result.RangesProven = contentRange.has_value() && start.has_value();
            result.ResponseValidator = std::move(responseValidator);
            result.Start = start;
            result.Worker = std::move(worker);
            result.Id = id;
            return result;
        }

        void Drive()
        {
            for (;;)
            {
                if (Cancelled())
                {
                    ThrowIfStopped();
                }
                auto now = Clock::now();
                ReapRetired();
                CheckStalls(now);
                ResolveParked();
                // Done only once no connection is left: one still reading past its
                // segment's end has an overlap check to finish, and the last one
                // has the end of the body to reach.
                if (m_table->IsComplete() && m_connections.empty())
                {
                    return;
                }
                Fill(now);
                if (m_connections.empty() && (!m_multiAllowed || !m_table->NextRetry(now)))
                {
                    // Nothing serves the rest and nothing will: the run is over.
                    throw Retryable(DownloadFailureCode::Network);
                }
                auto event = m_events.WaitFor(NextWake(now));
                now = Clock::now();
                if (event)
                {
                    Dispatch(std::move(*event), now);
                }
                Report(now);
            }
        }

        [[nodiscard]] Clock::duration NextWake(Clock::time_point now) const
        {
            auto wake = now + m_options.Timeouts.CancelPoll;
            if (m_multiAllowed)
            {
                if (auto const ramp = m_budget->NextRamp(); ramp && *ramp > now)
                {
                    wake = (std::min)(wake, *ramp);
                }
            }
            if (auto const retry = m_table->NextRetry(now))
            {
                wake = (std::min)(wake, *retry);
            }
            return wake > now ? wake - now : Clock::duration::zero();
        }

        void Dispatch(ConnectionEvent event, Clock::time_point now)
        {
            if (!m_connections.contains(event.Id))
            {
                // A connection already let go.
                return;
            }
            switch (event.Type)
            {
            case EventType::Opened:
                OnOpen(event.Id, event.Head, now);
                break;
            case EventType::OpenFailed:
                Fail(event.Id, event.Failure, std::nullopt, now);
                break;
            case EventType::Chunk:
            {
                CheckSpace(event.Data.size(), now);
                auto flow = Consume(event.Id, std::move(event.Data));
                Apply(event.Id, std::move(flow));
                break;
            }
            case EventType::End:
                OnEndOfBody(event.Id, now);
                break;
            case EventType::ReadFailed:
                Fail(event.Id, Retryable(DownloadFailureCode::Network), std::nullopt, now);
                break;
            }
        }

        // Lets one more connection in when the budget allows, and gives every free
        // slot work. Until a 206 shows the source serves ranges, only one extra
        // connection asks, so a source that ignores ranges costs one request.
        void Fill(Clock::time_point now)
        {
            if (!m_multiAllowed)
            {
                return;
            }
            m_budget->Ramp(now);
            auto const limit = m_rangesProven
                ? m_budget->Allowed()
                : (std::min)(m_budget->Allowed(), std::size_t{ 2 });
            while (m_connections.size() < limit)
            {
                auto const step = m_table->Choose(now, m_options.Tuning.MinSplitBytes);
                if (!step)
                {
                    break;
                }
                StartTable();
                auto const key = m_table->TakeStep(*step);
                if (!key)
                {
                    break;
                }
                StartConnection(*key, now);
            }
        }

        // Turns the run into a segmented one before its first extra connection:
        // the table reaches disk first, then the part takes its full length, so a
        // full-length part never exists without the table that explains it.
        void StartTable()
        {
            if (m_tableStarted)
            {
                return;
            }
            if (!FlushFileBuffers(m_file.get()))
            {
                throw StorageFull();
            }
            WriteTable();
            m_tableStarted = true;
            SizeFile();
        }

        // Gives the part its full length, sparse where the volume allows it, so a
        // write far into the file does not make Windows zero-fill the gap.
        void SizeFile()
        {
            LARGE_INTEGER length{};
            if (!GetFileSizeEx(m_file.get(), &length))
            {
                throw StorageFull();
            }
            if (static_cast<std::uint64_t>(length.QuadPart) != m_total)
            {
                MarkSparse();
                FILE_END_OF_FILE_INFO end{};
                end.EndOfFile.QuadPart = static_cast<LONGLONG>(m_total);
                if (!SetFileInformationByHandle(m_file.get(), FileEndOfFileInfo, &end, sizeof(end)))
                {
                    throw StorageFull();
                }
            }
            m_preallocated = !IsSparse();
        }

        // Volumes without sparse files (FAT, exFAT) refuse; the part is then
        // extended normally.
        void MarkSparse() const noexcept
        {
            DWORD returned{};
            static_cast<void>(DeviceIoControl(
                m_file.get(), FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr));
        }

        [[nodiscard]] bool IsSparse() const noexcept
        {
            BY_HANDLE_FILE_INFORMATION information{};
            return GetFileInformationByHandle(m_file.get(), &information)
                && (information.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0;
        }

        void WriteTable() const
        {
            if (!m_validator)
            {
                throw StorageFull();
            }
            try
            {
                HaloDesktop::Storage::WriteUtf8FileAtomic(
                    m_tablePath,
                    Segments::Encode(m_table->Saved(m_total, *m_validator)));
            }
            catch (...)
            {
                throw StorageFull();
            }
        }

        void StartConnection(std::uint32_t key, Clock::time_point now)
        {
            auto const* segment = m_table->Get(key);
            if (!segment)
            {
                return;
            }
            auto const requested = segment->Position;
            auto [worker, id] = NewWorker();
            m_table->Begin(key, id);
            Connection connection;
            connection.Segment = key;
            connection.StartedAt = requested;
            connection.Probe = !m_rangesProven;
            connection.State = ConnectionState::Opening;
            connection.LastActivity = now;
            connection.OpenedAt = now;
            connection.Worker = std::move(worker);
            connection.Worker->Open(OpenCommand{
                .Url = m_job.Url,
                .Headers = m_job.Headers,
                .Start = requested,
                .Validator = m_validator,
            });
            m_connections.emplace(id, std::move(connection));
        }

        void OnOpen(std::uint64_t id, Http::ResponseHead const& head, Clock::time_point now)
        {
            auto const status = head.Status;
            if (status == 200)
            {
                RangeRefused(id, now);
                return;
            }
            if (auto failure = ClassifyStatus(status))
            {
                auto const retryAfter = Http::RetryAfterSeconds(head);
                // Some hosts turn away one connection too many with 403 or 404.
                // While others still receive, that is a limit, not an expired
                // link; the last connection meeting it still ends the download.
                auto const receiving = std::any_of(m_connections.begin(), m_connections.end(), [id](auto const& pair)
                {
                    return pair.first != id && pair.second.State != ConnectionState::Opening;
                });
                if ((status == 403 || status == 404) && failure->Kind == TransferFailureKind::Permanent && receiving)
                {
                    failure = Retryable(DownloadFailureCode::ServerUnavailable);
                }
                Fail(id, *failure, retryAfter, now);
                return;
            }
            // A 206 must continue the same object exactly where it was asked to.
            auto const& connection = m_connections.at(id);
            auto const range = head.ContentRange ? Downloads::ParseContentRange(*head.ContentRange) : std::nullopt;
            if (!range)
            {
                throw Permanent(DownloadFailureCode::InvalidRange);
            }
            auto const lengthMatches = !head.ContentLength
                || *head.ContentLength == range->End - range->Start + 1;
            auto const fresh = Http::ValidatorOf(head);
            auto const validatorMatches = !fresh || m_validator == fresh;
            if (range->Start != connection.StartedAt
                || range->Total != m_total
                || !lengthMatches
                || !validatorMatches)
            {
                throw Permanent(DownloadFailureCode::InvalidRange);
            }
            m_rangesProven = true;
            Arm(id);
        }

        // An extra connection got the whole file instead of its range. Before any
        // 206 that means the source ignores ranges: the range goes back to the
        // connection before it, which reads on, and the run stays on one
        // connection. After a 206 it is a refusal like any other.
        void RangeRefused(std::uint64_t id, Clock::time_point now)
        {
            if (m_rangesProven)
            {
                Fail(id, Retryable(DownloadFailureCode::ServerUnavailable), std::nullopt, now);
                return;
            }
            auto connection = TakeConnection(id);
            if (!connection)
            {
                return;
            }
            m_table->Release(connection->Segment);
            m_multiAllowed = false;
            auto const* previous = m_table->PreviousOf(connection->Segment);
            if (!previous || !m_table->AbsorbNext(previous->Key))
            {
                throw Retryable(DownloadFailureCode::ServerUnavailable);
            }
        }

        // Removes the connection and lets its worker go: the in-flight call is
        // closed now, and the thread is joined once it is idle.
        [[nodiscard]] std::optional<Connection> TakeConnection(std::uint64_t id)
        {
            auto const found = m_connections.find(id);
            if (found == m_connections.end())
            {
                return std::nullopt;
            }
            auto connection = std::move(found->second);
            m_connections.erase(found);
            if (connection.Worker)
            {
                connection.Worker->Abort();
                m_retired.push_back(std::move(connection.Worker));
            }
            return connection;
        }

        void ReapRetired()
        {
            std::erase_if(m_retired, [](std::unique_ptr<ConnectionWorker> const& worker)
            {
                return worker->Idle();
            });
        }

        // Ends a connection that failed. With others still receiving, after the
        // source proved it serves ranges (or for the connection that was asking
        // whether it does), the run keeps the connections it has: the ceiling
        // drops to them and the segment waits to be asked for again. Otherwise the
        // failure ends the run, and the engine's own retries take over.
        void Fail(
            std::uint64_t id,
            TransferFailure failure,
            std::optional<std::uint64_t> retryAfter,
            Clock::time_point now)
        {
            auto connection = TakeConnection(id);
            if (!connection)
            {
                return;
            }
            m_table->Release(connection->Segment);
            if (failure.Kind != TransferFailureKind::Retryable)
            {
                throw failure;
            }
            if (auto const* segment = m_table->Get(connection->Segment); segment && segment->IsComplete())
            {
                // Its segment is all on disk; only the overlap past it is lost.
                return;
            }
            auto const receiving = static_cast<std::size_t>(std::count_if(
                m_connections.begin(), m_connections.end(), [](auto const& pair)
                {
                    return pair.second.State != ConnectionState::Opening;
                }));
            if (receiving == 0 || !(m_rangesProven || connection->Probe))
            {
                throw failure;
            }
            m_budget->Refuse(receiving);
            if (auto* segment = m_table->GetMutable(connection->Segment))
            {
                if (segment->Position > connection->StartedAt)
                {
                    // Data arrived first: not one failure in a row after another.
                    segment->Attempts = 0;
                }
                ++segment->Attempts;
                segment->RetryAt = now + Segments::RetryDelay(segment->Attempts, retryAfter);
            }
        }

        void Release(std::uint64_t id)
        {
            auto connection = TakeConnection(id);
            if (!connection)
            {
                return;
            }
            m_table->Release(connection->Segment);
            if (auto* segment = m_table->GetMutable(connection->Segment))
            {
                segment->Attempts = 0;
            }
        }

        // Asks connection `id` for its next chunk.
        void Arm(std::uint64_t id)
        {
            auto const found = m_connections.find(id);
            if (found == m_connections.end())
            {
                return;
            }
            found->second.State = ConnectionState::Reading;
            found->second.LastActivity = Clock::now();
            found->second.Worker->Read();
        }

        void Apply(std::uint64_t id, Flow flow)
        {
            switch (flow.Kind)
            {
            case FlowKind::Continue:
                Arm(id);
                break;
            case FlowKind::Park:
                if (auto const found = m_connections.find(id); found != m_connections.end())
                {
                    found->second.State = ConnectionState::Parked;
                    found->second.Held = std::move(flow.Held);
                }
                break;
            case FlowKind::Stop:
                Release(id);
                break;
            }
        }

        void OnEndOfBody(std::uint64_t id, Clock::time_point now)
        {
            auto const found = m_connections.find(id);
            if (found == m_connections.end())
            {
                return;
            }
            auto* segment = m_table->GetMutable(found->second.Segment);
            if (!segment)
            {
                return;
            }
            if (segment->End == Segments::Unbounded)
            {
                // A body of unstated size ends where the source ends it, and is
                // then held to the size the addon reported, if any.
                auto const arrived = segment->Position;
                if (m_total > 0 && arrived > m_total)
                {
                    throw Permanent(DownloadFailureCode::InvalidRange);
                }
                if (m_total > 0 && arrived < m_total)
                {
                    Fail(id, Retryable(DownloadFailureCode::Network), std::nullopt, now);
                    return;
                }
                segment->End = arrived;
                m_total = arrived;
                Release(id);
                return;
            }
            if (segment->Position >= segment->End)
            {
                Release(id);
                return;
            }
            // The source closed the body before the segment was complete.
            Fail(id, Retryable(DownloadFailureCode::Network), std::nullopt, now);
        }

        // Writes what belongs to the connection's segment and decides what the
        // connection does next. Bytes past the end are never written: they are the
        // overlap check's, or a parked connection's to hold.
        [[nodiscard]] Flow Consume(std::uint64_t id, std::vector<std::uint8_t> data)
        {
            auto const found = m_connections.find(id);
            if (found == m_connections.end())
            {
                throw Permanent(DownloadFailureCode::Unknown);
            }
            auto const key = found->second.Segment;
            std::size_t offset{};
            for (;;)
            {
                auto const* segment = m_table->Get(key);
                if (!segment)
                {
                    throw Permanent(DownloadFailureCode::Unknown);
                }
                auto const position = segment->Position;
                auto const end = segment->End;
                auto const left = data.size() - offset;
                if (position < end)
                {
                    if (left == 0)
                    {
                        return Flow{};
                    }
                    auto const take = static_cast<std::size_t>((std::min)(end - position, static_cast<std::uint64_t>(left)));
                    std::span<std::uint8_t const> const piece{ data.data() + offset, take };
                    WriteAt(position, piece);
                    if (m_table->Record(key, piece) == Segments::OverlapCheck::Mismatch)
                    {
                        throw Mismatch();
                    }
                    offset += take;
                    continue;
                }
                if (!m_table->NextOf(key))
                {
                    if (left > 0)
                    {
                        // More than the whole file.
                        throw Permanent(DownloadFailureCode::InvalidRange);
                    }
                    return Flow{};
                }
                if (!m_rangesProven)
                {
                    if (left == 0)
                    {
                        return Flow{};
                    }
                    return Flow{
                        .Kind = FlowKind::Park,
                        .Held = std::vector<std::uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(offset), data.end()),
                    };
                }
                auto const wanted = m_table->OverlapWanted(key, m_total);
                auto& connection = found->second;
                auto const missing = wanted > connection.Overlap.size() ? wanted - connection.Overlap.size() : 0;
                auto const take = (std::min)(missing, left);
                connection.Overlap.insert(
                    connection.Overlap.end(),
                    data.begin() + static_cast<std::ptrdiff_t>(offset),
                    data.begin() + static_cast<std::ptrdiff_t>(offset + take));
                if (connection.Overlap.size() < wanted)
                {
                    return Flow{};
                }
                auto overlap = std::exchange(connection.Overlap, {});
                if (!overlap.empty()
                    && m_table->HandOverOverlap(key, std::move(overlap)) == Segments::OverlapCheck::Mismatch)
                {
                    throw Mismatch();
                }
                return Flow{ .Kind = FlowKind::Stop };
            }
        }

        [[nodiscard]] TransferFailure Mismatch() noexcept
        {
            m_corrupt = true;
            return Permanent(DownloadFailureCode::InvalidRange);
        }

        // Lets parked connections go once the question they wait on is answered:
        // ranges are proven, or the next segment has no connection and nothing of
        // it arrived, so its range comes back to them.
        void ResolveParked()
        {
            std::vector<std::uint64_t> parked;
            for (auto const& [id, connection] : m_connections)
            {
                if (connection.State == ConnectionState::Parked) parked.push_back(id);
            }
            for (auto const id : parked)
            {
                auto const found = m_connections.find(id);
                if (found == m_connections.end())
                {
                    continue;
                }
                auto const key = found->second.Segment;
                auto const free = m_rangesProven
                    || !m_table->NextOf(key)
                    || m_table->AbsorbNext(key);
                if (!free)
                {
                    continue;
                }
                found->second.State = ConnectionState::Opening;
                auto held = std::exchange(found->second.Held, {});
                auto flow = Consume(id, std::move(held));
                Apply(id, std::move(flow));
            }
        }

        // A connection that delivers nothing for the read timeout has failed, and
        // so has an extra connection whose answer takes as long. A parked one is
        // waiting on purpose.
        void CheckStalls(Clock::time_point now)
        {
            std::vector<std::uint64_t> stalled;
            for (auto const& [id, connection] : m_connections)
            {
                auto const reading = connection.State == ConnectionState::Reading
                    && now - connection.LastActivity >= m_options.Timeouts.Read;
                auto const opening = connection.State == ConnectionState::Opening
                    && now - connection.OpenedAt >= m_options.Timeouts.Read;
                if (reading || opening) stalled.push_back(id);
            }
            for (auto const id : stalled)
            {
                Fail(id, Retryable(DownloadFailureCode::Network), std::nullopt, now);
            }
        }

        [[nodiscard]] std::uint64_t NeededSpace(std::uint64_t chunk) const noexcept
        {
            if (m_preallocated)
            {
                return chunk;
            }
            auto const received = m_table->Received();
            auto const remaining = m_total > received ? m_total - received : 0;
            return (std::max)(remaining, chunk);
        }

        void CheckInitialSpace() const
        {
            if (auto const free = AvailableBytes(m_job.Root);
                free && !Downloads::HasSufficientSpace(*free, NeededSpace(0)))
            {
                throw StorageFull();
            }
        }

        // Rechecked every few megabytes rather than on every chunk: the reserve
        // absorbs what lands in between, and a response that never states its size
        // still cannot eat into the reserve.
        void CheckSpace(std::uint64_t chunk, Clock::time_point now)
        {
            m_uncheckedBytes += chunk;
            if (m_uncheckedBytes < SpaceCheckBytes && now - m_lastSpaceCheck < m_options.Timeouts.Progress)
            {
                return;
            }
            if (auto const free = AvailableBytes(m_job.Root);
                free && !Downloads::HasSufficientSpace(*free, NeededSpace(chunk)))
            {
                throw StorageFull();
            }
            m_uncheckedBytes = 0;
            m_lastSpaceCheck = now;
        }

        void WriteAt(std::uint64_t offset, std::span<std::uint8_t const> data)
        {
            ThrowIfStopped();
            std::size_t written{};
            while (written < data.size())
            {
                auto const position = offset + written;
                OVERLAPPED at{};
                at.Offset = static_cast<DWORD>(position & 0xffffffffull);
                at.OffsetHigh = static_cast<DWORD>(position >> 32);
                auto const count = static_cast<DWORD>((std::min)(
                    data.size() - written,
                    static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
                DWORD done{};
                if (!WriteFile(m_file.get(), data.data() + written, count, &done, &at) || done == 0)
                {
                    throw StorageFull();
                }
                written += done;
            }
        }

        void Report(Clock::time_point now)
        {
            if (now - m_lastProgress < m_options.Timeouts.Progress)
            {
                return;
            }
            auto const received = m_table->Received();
            auto const durable = now - m_lastDurable >= m_options.Timeouts.Durable;
            if (durable)
            {
                MakeDurable();
                m_lastDurable = now;
            }
            // Persisting only after a flush keeps the stored byte count, and the
            // table, at or below what is actually on disk.
            auto const rate = m_rate.Record(now, received);
            if (m_host.Progress)
            {
                m_host.Progress(Downloads::SegmentedProgress{
                    .Bytes = received,
                    .Total = m_total,
                    .BytesPerSecond = rate,
                    .Connections = ReceivingConnections(),
                    .Persist = durable,
                });
            }
            m_lastProgress = now;
        }

        [[nodiscard]] std::uint32_t ReceivingConnections() const noexcept
        {
            auto const reading = std::count_if(m_connections.begin(), m_connections.end(), [](auto const& pair)
            {
                return pair.second.State == ConnectionState::Reading;
            });
            return static_cast<std::uint32_t>(reading);
        }

        void MakeDurable()
        {
            if (!FlushFileBuffers(m_file.get()))
            {
                throw StorageFull();
            }
            if (m_tableStarted)
            {
                WriteTable();
            }
        }

        [[nodiscard]] Downloads::SegmentedFinished Finish()
        {
            if (!FlushFileBuffers(m_file.get()))
            {
                throw StorageFull();
            }
            LARGE_INTEGER length{};
            if (!GetFileSizeEx(m_file.get(), &length))
            {
                throw StorageFull();
            }
            if (static_cast<std::uint64_t>(length.QuadPart) != m_total)
            {
                throw Permanent(DownloadFailureCode::InvalidRange);
            }
            m_file.reset();
            // Every byte is flushed, so the bare part is now the truth.
            if (!RemoveIfPresent(m_tablePath))
            {
                throw StorageFull();
            }
            return Downloads::SegmentedFinished{
                .Bytes = m_total,
                .Total = m_total,
                .Validator = std::move(m_responseValidator),
            };
        }

        // Leaves what a later run can resume: the table as of a fresh flush. A
        // mismatch leaves nothing, part first, so a full-length part never
        // outlives its table.
        void Abandon() noexcept
        {
            for (auto& [id, connection] : m_connections)
            {
                static_cast<void>(id);
                if (connection.Worker) connection.Worker->Abort();
            }
            if (m_corrupt)
            {
                m_file.reset();
                if (DeleteFileW(m_job.Part.c_str()))
                {
                    static_cast<void>(DeleteFileW(m_tablePath.c_str()));
                }
                return;
            }
            if (!m_tableStarted || !m_file || !m_table)
            {
                return;
            }
            std::error_code error;
            // A removal may have deleted the part already; a table must not
            // reappear beside nothing.
            if (std::filesystem::exists(m_job.Part, error) && !error && FlushFileBuffers(m_file.get()))
            {
                try
                {
                    WriteTable();
                }
                catch (...)
                {
                    // Best effort: the table already on disk is still a safe one.
                }
            }
        }

        Downloads::SegmentedJob const& m_job;
        std::atomic_bool const& m_cancel;
        std::stop_token m_stop;
        Downloads::SegmentedHost const& m_host;
        Downloads::SegmentedOptions const& m_options;
        // Declared before every worker, so it outlives the threads that post to it.
        EventQueue m_events;
        wil::unique_hfile m_file;
        std::filesystem::path m_tablePath;
        std::optional<Segments::SegmentTable> m_table;
        // The size of the file; 0 while unknown.
        std::uint64_t m_total{};
        std::optional<std::wstring> m_responseValidator;
        std::optional<std::wstring> m_validator;
        // More than one connection may serve this run.
        bool m_multiAllowed{};
        bool m_rangesProven{};
        // The sidecar exists and the part has its full length.
        bool m_tableStarted{};
        // The full length is allocated, so writes take no further space.
        bool m_preallocated{};
        // Two connections disagreed about the same bytes.
        bool m_corrupt{};
        std::optional<Segments::ConnectionBudget> m_budget;
        std::uint64_t m_nextConnection{};
        Downloads::TransferRateWindow m_rate;
        Clock::time_point m_lastProgress;
        Clock::time_point m_lastDurable;
        Clock::time_point m_lastSpaceCheck;
        std::uint64_t m_uncheckedBytes{};
        // Workers let go but not yet joined; joined once idle, or at the end.
        std::vector<std::unique_ptr<ConnectionWorker>> m_retired;
        std::map<std::uint64_t, Connection> m_connections;
    };
}

namespace HaloDesktop::Services::Downloads
{
    SegmentedFinished RunSegmentedTransfer(
        SegmentedJob const& job,
        std::atomic_bool const& cancel,
        std::stop_token stop,
        SegmentedHost const& host,
        SegmentedOptions const& options)
    {
        Coordinator coordinator{ job, cancel, std::move(stop), host, options };
        return coordinator.Execute();
    }
}
