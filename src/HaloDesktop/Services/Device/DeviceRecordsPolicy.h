#pragma once

#include "Api/Dto.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <winrt/Windows.Data.Json.h>

namespace HaloDesktop::Services::Device
{
    // Validation and last-write-wins merging for the records Halo keeps on
    // this PC without an account, mirroring the server's request schemas and
    // upserts as the reference's `device/records.ts` does: what is stored is
    // always something the server would accept, so the records can later move
    // into an account. A refusal is the server's sentence for a 400. No I/O.
    //
    // The typed rows cannot hold a field of the wrong JSON type or a
    // fractional timestamp, so those checks of the reference are met by
    // construction.

    inline constexpr std::size_t MaximumAddons = 50;
    inline constexpr std::size_t MaximumWatchStateNameLength = 512;

    // Same rules as the server's PUT /addons, http(s) only.
    [[nodiscard]] std::optional<std::wstring> AddonUrlsProblem(std::vector<std::wstring> const& urls);
    [[nodiscard]] std::optional<std::wstring> LibraryItemProblem(Api::Dto::LibraryRow const& item);
    [[nodiscard]] std::optional<std::wstring> WatchStateProblem(Api::Dto::WatchEntry const& state);
    // Known settings are checked; unknown ones pass through, as on the server,
    // so newer clients keep theirs.
    [[nodiscard]] std::optional<std::wstring> SettingsProblem(
        winrt::Windows::Data::Json::JsonObject const& value,
        std::int64_t updatedAt);

    // The first problem among the rows, in order.
    [[nodiscard]] std::optional<std::wstring> LibraryProblem(std::vector<Api::Dto::LibraryRow> const& items);
    [[nodiscard]] std::optional<std::wstring> WatchStatesProblem(std::vector<Api::Dto::WatchEntry> const& states);

    // Incoming rows over stored ones, keyed by id: a strictly newer UpdatedAt
    // wins and a tie keeps the stored row. A row's type is fixed by its first
    // write. Removals are tombstones (RemovedAt), never deletions, so they
    // survive stale re-adds. Stored order is kept; new ids are appended.
    [[nodiscard]] std::vector<Api::Dto::LibraryRow> MergeLibrary(
        std::vector<Api::Dto::LibraryRow> const& stored,
        std::vector<Api::Dto::LibraryRow> const& incoming);

    // The same rule keyed by video id; a winning row replaces the stored one
    // whole.
    [[nodiscard]] std::vector<Api::Dto::WatchEntry> MergeWatchStates(
        std::vector<Api::Dto::WatchEntry> const& stored,
        std::vector<Api::Dto::WatchEntry> const& incoming);

    // What a watch history read returns: everything but removal tombstones.
    [[nodiscard]] std::vector<Api::Dto::WatchEntry> LiveWatchStates(std::vector<Api::Dto::WatchEntry> const& states);
}
