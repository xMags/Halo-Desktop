#pragma once

#include "Services/Downloads/DownloadTypes.h"
#include "Services/Downloads/SegmentPlanner.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <stop_token>
#include <string>

// Runs one transfer of one download over as many as eight HTTP connections.
// Debrid CDNs cap each connection's speed, so a download that can be fetched in
// ranges is fetched in several at once.
//
// The first connection is the request the engine always made, with the same
// redirect, header, validator and resume rules. More connections join only once
// the size is stated, the source gave a validator, the body is not encoded and
// Accept-Ranges is not "none"; until some answer in this run proves ranges work
// (a 206 with the expected Content-Range) at most two connections exist. One
// joins every ramp interval: it resumes the unassigned segment with the most
// left, or splits the busiest one. A refusal while others still receive fixes
// the run's connection ceiling. Every connection reads a little past its
// segment's end, and those bytes must match what the next segment's own
// connection received, or the source served two different objects.
//
// Threading model. The calling thread is the coordinator: it alone owns the
// part file, the segment table, the sidecar and every decision. Each connection
// has one worker thread that performs blocking WinHTTP calls one at a time, only
// when the coordinator asks (open, or read one chunk), and posts the result to
// the coordinator's event queue. The queue's mutex is the only lock the two
// sides share, and it is never held across network or file work; the table
// needs no lock because only the coordinator touches it. File writes are
// positioned (OVERLAPPED offsets) and made by the coordinator alone. To stop a
// connection the coordinator closes its in-flight request handle, which WinHTTP
// documents as the way to end a pending synchronous call, so no worker is ever
// left waiting on the network. Every worker thread is joined before the run
// returns, so no thread outlives the transfer or the engine that started it.
//
// Synchronous WinHTTP on one thread per connection was chosen over WinHTTP's
// asynchronous mode because the engine's request code (redirects by hand,
// protected-header stripping, header parsing) is synchronous and tested, and is
// reused unchanged; at most eight connections run at once, so the thread cost
// is small.
//
// The table goes to a sidecar beside the part, written only after the part is
// flushed, so a later run resumes each segment and never trusts a byte that was
// not on disk. A part without a sidecar is trusted up to the smaller of its
// length and the last count stored after a flush (SegmentedJob::FlushedBytes),
// and cut back to it.
namespace HaloDesktop::Services::Downloads
{
    // Every wait the transfer makes. Production uses the defaults; tests shorten
    // them so nothing waits for real.
    struct TransferTimeouts final
    {
        // How quickly a pause is noticed while a request waits for its answer.
        std::chrono::milliseconds CancelPoll{ 250 };
        // The longest the first request may wait for its response.
        std::chrono::milliseconds FirstResponse{ 60000 };
        // A connection that delivers nothing for this long has failed, and an
        // extra connection that is not answered in this long has too.
        std::chrono::milliseconds Read{ 45000 };
        std::chrono::milliseconds Progress{ 250 };
        // The part is flushed, the sidecar rewritten and the index persisted at
        // most this often.
        std::chrono::milliseconds Durable{ 2000 };
        std::chrono::milliseconds RateWindow{ 3000 };
    };

    struct SegmentedOptions final
    {
        Segments::Tuning Tuning;
        TransferTimeouts Timeouts;
    };

    enum class TransferFailureKind
    {
        Permanent,
        Retryable,
        Paused,
        Shutdown,
    };

    // Thrown by a transfer that did not finish.
    struct TransferFailure final
    {
        TransferFailureKind Kind{ TransferFailureKind::Permanent };
        DownloadFailureCode Code{ DownloadFailureCode::Unknown };
    };

    struct SegmentedJob final
    {
        std::wstring Url;
        std::map<std::wstring, std::wstring, std::less<>> Headers;
        std::filesystem::path Part;
        std::filesystem::path Root;
        // The size an earlier response stated, or 0.
        std::uint64_t KnownTotal{};
        // The validator an earlier response gave.
        std::optional<std::wstring> Validator;
        // The size the addon reported: an estimate for the free-space check
        // and the stand-in check, never the length the body must reach.
        std::optional<std::uint64_t> SizeHint;
        // The byte count last reported with Persist for this download (under
        // Validator), or 0 when unknown. A part without a sidecar is trusted
        // only up to it: after a power cut its length can include bytes that
        // never reached the disk. 0, or more than the part holds, says
        // nothing, and the part's length decides as it always did.
        std::uint64_t FlushedBytes{};
    };

    struct SegmentedProgress final
    {
        std::uint64_t Bytes{};
        std::uint64_t Total{};
        std::uint64_t BytesPerSecond{};
        // Connections receiving data right now: those reading a body, not those
        // still opening or parked at a split.
        std::uint32_t Connections{};
        // The part was flushed just before this report, so its count may be stored.
        bool Persist{};
    };

    // Where a transfer reports to. Both are called on the coordinator thread with
    // no transfer lock held. An exception from ResponseMetadata fails the
    // transfer as Permanent Unknown; one from Progress propagates unchanged.
    struct SegmentedHost final
    {
        std::function<void(std::optional<std::wstring> const& validator, std::uint64_t total)> ResponseMetadata;
        std::function<void(SegmentedProgress const& progress)> Progress;
    };

    // The part is complete and flushed, and its sidecar is gone.
    struct SegmentedFinished final
    {
        std::uint64_t Bytes{};
        std::uint64_t Total{};
        std::optional<std::wstring> Validator;
    };

    // Downloads `job` into its part file. Throws TransferFailure when it does not
    // finish: Paused once `cancel` is raised, Shutdown once `stop` is requested.
    // Leaves what a later run can resume, except after an overlap mismatch, which
    // deletes the part and then its sidecar.
    [[nodiscard]] SegmentedFinished RunSegmentedTransfer(
        SegmentedJob const& job,
        std::atomic_bool const& cancel,
        std::stop_token stop,
        SegmentedHost const& host,
        SegmentedOptions const& options = {});
}
