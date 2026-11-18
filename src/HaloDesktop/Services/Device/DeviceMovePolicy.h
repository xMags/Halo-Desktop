#pragma once

#include "Api/Dto.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <pplawait.h>
#include <ppltasks.h>

namespace HaloDesktop::Api
{
    class IHaloBackend;
}

namespace HaloDesktop::Services::Device
{
    // Moving Halo-without-an-account data into the account someone signs in
    // to (the reference's `device/mergeIntoAccount.ts`): library and watch
    // history merge with the account's (newest change wins, the server's
    // rule), the addons added on this PC join the account's own list, this
    // PC's settings are used only if the account has none yet. Every step is
    // safe to repeat, so a move that failed part way simply runs again.

    // Rows per request, so a long history is not one huge body.
    inline constexpr std::size_t MoveChunkSize = 500;

    struct AddonMove final
    {
        std::wstring TransportUrl;
        std::wstring Name;
        bool HideCatalogs{};

        friend bool operator==(AddonMove const&, AddonMove const&) = default;
    };

    struct MoveResult final
    {
        // Addons the account refused because their manifest did not load;
        // named to the user.
        std::vector<std::wstring> FailedAddons;
    };

    // The addons added on this PC, in their order, minus the built-ins (every
    // account installs those itself) and minus any the account already has in
    // its own list.
    [[nodiscard]] std::vector<AddonMove> AddonsToMove(
        std::span<Api::Dto::AddonRecord const> device,
        std::span<Api::Dto::AddonRecord const> accountOwn);

    // Settings are one document, saved whole and newest-wins, so this PC's
    // copy would overwrite anything tuned on another device. It is used only
    // when the account has never saved any.
    [[nodiscard]] bool ShouldMoveSettings(std::int64_t deviceUpdatedAt, std::int64_t accountUpdatedAt) noexcept;

    template <typename T>
    [[nodiscard]] std::vector<std::vector<T>> Chunked(std::vector<T> const& items, std::size_t size = MoveChunkSize)
    {
        std::vector<std::vector<T>> chunks;
        if (size == 0) return chunks;
        for (std::size_t start = 0; start < items.size(); start += size)
        {
            auto const end = start + (std::min)(size, items.size() - start);
            chunks.emplace_back(items.begin() + static_cast<std::ptrdiff_t>(start), items.begin() + static_cast<std::ptrdiff_t>(end));
        }
        return chunks;
    }

    // Every watch row this PC stores, removal tombstones included: the live
    // history leaves them out, and a removal made here must reach the account
    // to outlast its older copy of the video.
    using StoredWatchStatesReader = std::function<concurrency::task<std::vector<Api::Dto::WatchEntry>>()>;

    // Sends this PC's library, watch history, settings and addons to the
    // account, in that order. Throws when the account cannot be reached or
    // refuses anything other than an addon; the caller keeps the device data
    // and tries again. An addon whose manifest the account cannot load (a
    // 400) is skipped and named instead, so one dead addon cannot hold
    // everything else back. Addons go one at a time: the account installs a
    // list all or nothing.
    [[nodiscard]] concurrency::task<MoveResult> MoveRecordsIntoAccountAsync(
        std::shared_ptr<Api::IHaloBackend> device,
        StoredWatchStatesReader storedWatchStates,
        std::shared_ptr<Api::IHaloBackend> account);

    // The dialog after a move that skipped addons.
    [[nodiscard]] std::wstring FailedAddonsTitle(std::size_t count);
    [[nodiscard]] std::wstring FailedAddonsBody(std::vector<std::wstring> const& names);
}
