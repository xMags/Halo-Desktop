#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <pplawait.h>
#include <ppltasks.h>

namespace HaloDesktop::Api
{
    class ApiClient;
    class IHaloBackend;
}

namespace HaloDesktop::Api::Addons
{
    class IAddonFetcher;
}

namespace HaloDesktop::Services
{
    class IDownloadService;
    class SessionService;
}

namespace HaloDesktop::Services::Device
{
    class DeviceStore;

    // Moves this PC's data from Halo without an account (library, watch
    // history, settings, addons, downloads) into the signed-in account, then
    // clears it from this PC (the reference's `device/deviceDataMove.ts`).
    //
    // This PC keeps the only copy until every part has moved: the documents
    // and the profile are removed last, and only while the same account is
    // still signed in. Any failure keeps everything as it was and the move is
    // tried again at the next launch; every step is safe to repeat. Records
    // merge newest-wins, so older device rows never replace newer account
    // rows, and settings move only into an account that never saved any.
    //
    // UI-thread-only. Only one move runs at a time: a second call while one is
    // under way waits for that one.
    class DeviceDataMove final : public std::enable_shared_from_this<DeviceDataMove>
    {
    public:
        using SkippedAddonsHandler = std::function<void(std::vector<std::wstring> const& names)>;
        using SkippedAddonsToken = std::uint64_t;

        DeviceDataMove(
            std::shared_ptr<SessionService> session,
            std::shared_ptr<Api::ApiClient> account,
            std::shared_ptr<DeviceStore> store,
            std::shared_ptr<Api::Addons::IAddonFetcher> fetcher,
            std::shared_ptr<IDownloadService> downloads);

        DeviceDataMove(DeviceDataMove const&) = delete;
        DeviceDataMove& operator=(DeviceDataMove const&) = delete;

        // True once nothing is left to move (or there was nothing), false when
        // the move did not finish and is left for the next launch. Never
        // throws. seedDefaults: a first sign-in installs the built-in addons
        // before anything moves, so a moved addon does not count as the
        // account already having addons.
        [[nodiscard]] concurrency::task<bool> RunAsync(bool seedDefaults);

        // Told on the UI thread of addons a finished move skipped because the
        // account could not load them. Names nobody heard of yet are kept for
        // TakeUnreportedSkippedAddons.
        [[nodiscard]] SkippedAddonsToken AddSkippedAddonsHandler(SkippedAddonsHandler handler);
        void RemoveSkippedAddonsHandler(SkippedAddonsToken token) noexcept;
        [[nodiscard]] std::vector<std::wstring> TakeUnreportedSkippedAddons();

    private:
        [[nodiscard]] concurrency::task<bool> RunCoreAsync(bool seedDefaults);
        void ReportSkipped(std::vector<std::wstring> names);

        std::shared_ptr<SessionService> m_session;
        std::shared_ptr<Api::ApiClient> m_account;
        std::shared_ptr<DeviceStore> m_store;
        std::shared_ptr<Api::Addons::IAddonFetcher> m_fetcher;
        std::shared_ptr<IDownloadService> m_downloads;
        std::optional<concurrency::task<bool>> m_running;
        std::unordered_map<SkippedAddonsToken, SkippedAddonsHandler> m_handlers;
        SkippedAddonsToken m_nextToken{ 1 };
        std::vector<std::wstring> m_unreported;
    };

    // The reference's seedDefaultAddons, for an account's first sign-in: an
    // account with no addons at all gets the built-in ones. Best effort.
    [[nodiscard]] concurrency::task<void> SeedDefaultAddonsAsync(std::shared_ptr<Api::IHaloBackend> account);
}
