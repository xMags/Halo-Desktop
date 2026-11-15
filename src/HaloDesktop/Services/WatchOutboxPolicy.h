#pragma once

#include "Api/Dto.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::Services
{
    // The rules of the reference client's `player/watchOutbox.ts`: watch progress
    // that has not reached the server yet, kept on this PC so that a report that
    // fails, or a window closed mid-playback, costs nothing.

    // Far more videos than one sitting leaves unsent; a bound against a store
    // that only ever grows.
    inline constexpr std::size_t WatchOutboxMaximumRows = 200;

    // The file is a few kilobytes at its bound. Anything larger is not one this
    // app wrote, and is ignored and replaced rather than parsed.
    inline constexpr std::uint64_t WatchOutboxMaximumBytes = 1024u * 1024u;

    // `incoming` over `pending`, one row per video: the newer UpdatedAt wins, a
    // tie keeps what was there. Oldest first; over the bound, the oldest go.
    [[nodiscard]] std::vector<Api::Dto::WatchEntry> MergePendingWatchRows(
        std::vector<Api::Dto::WatchEntry> const& pending,
        std::vector<Api::Dto::WatchEntry> const& incoming);

    // `pending` without the rows `sent` covered. A row staged again since, with a
    // newer time, stays.
    [[nodiscard]] std::vector<Api::Dto::WatchEntry> WithoutSettledWatchRows(
        std::vector<Api::Dto::WatchEntry> const& pending,
        std::vector<Api::Dto::WatchEntry> const& sent);

    // Sync is last write wins, so a pending row the server already holds an
    // equal or newer row for would change nothing there. Those are settled
    // without being sent; the others go out.
    struct PendingWatchSend final
    {
        std::vector<Api::Dto::WatchEntry> ToSend;
        std::vector<Api::Dto::WatchEntry> AlreadyKnown;
    };

    [[nodiscard]] PendingWatchSend SplitPendingWatchRows(
        std::vector<Api::Dto::WatchEntry> const& pending,
        std::vector<Api::Dto::WatchEntry> const& known);

    // The stored form: the account the rows were watched under (an opaque hash
    // of the server and user, never a credential) and the rows themselves.
    [[nodiscard]] std::string SerializeWatchOutbox(
        std::wstring_view accountKey,
        std::vector<Api::Dto::WatchEntry> const& rows);

    // The rows a stored outbox holds for `accountKey`. Empty for anything else:
    // another account or server, an unreadable or unexpected document. A row
    // that is malformed is dropped on its own.
    [[nodiscard]] std::vector<Api::Dto::WatchEntry> ParseWatchOutbox(
        std::string const& text,
        std::wstring_view accountKey);

    // Whether two history snapshots hold the same rows in the same order, so a
    // read that changed nothing need not redraw anything.
    [[nodiscard]] bool SameWatchRows(
        std::vector<Api::Dto::WatchEntry> const& left,
        std::vector<Api::Dto::WatchEntry> const& right) noexcept;
}
