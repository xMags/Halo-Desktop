#include "pch.h"
#include "Services/Device/DeviceDataMove.h"

#include "Api/Addons/AddonFetcher.h"
#include "Api/ApiClient.h"
#include "Api/HaloBackend.h"
#include "Services/AddonListPolicy.h"
#include "Services/Device/DeviceBackend.h"
#include "Services/Device/DeviceDocuments.h"
#include "Services/Device/DeviceMovePolicy.h"
#include "Services/Device/DeviceStore.h"
#include "Services/Downloads/DownloadTypes.h"
#include "Services/ServiceInterfaces.h"
#include "Services/SessionService.h"

#include <stdexcept>
#include <utility>

namespace HaloDesktop::Services::Device
{
    concurrency::task<void> SeedDefaultAddonsAsync(std::shared_ptr<Api::IHaloBackend> account)
    {
        try
        {
            auto const existing = co_await account->GetAddonsAsync();
            if (!existing.Global.empty() || !existing.User.empty()) co_return;
            std::vector<winrt::hstring> defaults;
            defaults.reserve(::HaloDesktop::Services::DefaultAddonUrls.size());
            for (auto const url : ::HaloDesktop::Services::DefaultAddonUrls) defaults.emplace_back(url);
            co_await account->PutAddonsAsync(std::move(defaults));
        }
        catch (...)
        {
            // Best effort: the sign-in still succeeds when a built-in is down.
        }
    }

    DeviceDataMove::DeviceDataMove(
        std::shared_ptr<SessionService> session,
        std::shared_ptr<Api::ApiClient> account,
        std::shared_ptr<DeviceStore> store,
        std::shared_ptr<Api::Addons::IAddonFetcher> fetcher,
        std::shared_ptr<IDownloadService> downloads)
        : m_session(std::move(session)),
          m_account(std::move(account)),
          m_store(std::move(store)),
          m_fetcher(std::move(fetcher)),
          m_downloads(std::move(downloads))
    {
        if (!m_session || !m_account || !m_store || !m_fetcher || !m_downloads)
        {
            throw std::invalid_argument{ "DeviceDataMove requires all dependencies." };
        }
    }

    concurrency::task<bool> DeviceDataMove::RunAsync(bool seedDefaults)
    {
        if (m_running)
        {
            auto running = *m_running;
            co_return co_await running;
        }
        auto const uiContext = winrt::apartment_context{};
        auto task = RunCoreAsync(seedDefaults);
        m_running = task;
        auto moved = false;
        try
        {
            moved = co_await task;
        }
        catch (...)
        {
            moved = false;
        }
        co_await uiContext;
        m_running.reset();
        co_return moved;
    }

    concurrency::task<bool> DeviceDataMove::RunCoreAsync(bool seedDefaults)
    {
        auto const self = shared_from_this();
        auto const uiContext = winrt::apartment_context{};
        auto const profile = m_session->PendingDeviceProfile();
        if (!profile)
        {
            co_return true;
        }
        auto const owner = m_session->DataOwner();
        if (m_session->Mode() != SessionMode::SignedIn || !owner)
        {
            co_return false;
        }
        // The move belongs to this account; anything else signing in meanwhile
        // stops it before this PC's copy is touched.
        auto const generation = m_session->Generation();
        auto const stillThisAccount = [this, generation, &profile]()
        {
            auto const pending = m_session->PendingDeviceProfile();
            return m_session->Generation() == generation && pending && pending->Id == profile->Id;
        };

        // Checked again before every request to the account, off the UI thread,
        // so a switch mid-move stops it at the next request rather than after
        // the rest of this PC's data reached someone else's account.
        auto const accountGeneration = m_session->AccountSessionGeneration();
        std::shared_ptr<Api::IHaloBackend> const account = std::make_shared<OwnerCheckedBackend>(
            m_account,
            [session = m_session, accountGeneration]() { return session->AccountSessionGeneration() == accountGeneration; });
        auto const device = std::make_shared<DeviceBackend>(DeviceBackendOptions{ .Store = m_store, .Fetcher = m_fetcher });
        std::optional<MoveResult> result;
        try
        {
            if (seedDefaults)
            {
                co_await SeedDefaultAddonsAsync(account);
            }
            result = co_await MoveRecordsIntoAccountAsync(
                device,
                [device]() { return device->GetStoredWatchStatesAsync(); },
                account);
        }
        catch (...)
        {
            result.reset();
        }
        co_await uiContext;
        if (!result || !stillThisAccount())
        {
            co_return false;
        }

        auto adopted = false;
        try
        {
            co_await m_downloads->AdoptAccountAsync(
                DeviceDownloadOwner(*profile),
                ::HaloDesktop::Services::Downloads::MakeAccountKey(owner->Server, owner->User));
            adopted = true;
        }
        catch (...)
        {
            adopted = false;
        }
        co_await uiContext;
        if (!adopted || !stillThisAccount())
        {
            co_return false;
        }

        // Only after every part has moved: until then this PC keeps the only copy.
        auto const store = m_store;
        auto cleared = false;
        try
        {
            co_await store->ClearAsync();
            cleared = true;
        }
        catch (...)
        {
            cleared = false;
        }
        co_await uiContext;
        if (!cleared)
        {
            co_return false;
        }
        m_session->ForgetDeviceProfile(profile->Id);
        // What every page shows now includes what came from this PC.
        if (m_session->Generation() == generation)
        {
            m_session->ReloadAccountData();
        }
        if (!result->FailedAddons.empty())
        {
            ReportSkipped(std::move(result->FailedAddons));
        }
        co_return true;
    }

    void DeviceDataMove::ReportSkipped(std::vector<std::wstring> names)
    {
        if (m_handlers.empty())
        {
            m_unreported.insert(m_unreported.end(), names.begin(), names.end());
            return;
        }
        // Copied first: a handler is free to unsubscribe from inside itself.
        std::vector<SkippedAddonsHandler> handlers;
        handlers.reserve(m_handlers.size());
        for (auto const& [token, handler] : m_handlers)
        {
            static_cast<void>(token);
            handlers.push_back(handler);
        }
        for (auto const& handler : handlers)
        {
            try
            {
                handler(names);
            }
            catch (...)
            {
            }
        }
    }

    DeviceDataMove::SkippedAddonsToken DeviceDataMove::AddSkippedAddonsHandler(SkippedAddonsHandler handler)
    {
        if (!handler)
        {
            return 0;
        }
        auto const token = m_nextToken++;
        m_handlers.emplace(token, std::move(handler));
        return token;
    }

    void DeviceDataMove::RemoveSkippedAddonsHandler(SkippedAddonsToken token) noexcept
    {
        m_handlers.erase(token);
    }

    std::vector<std::wstring> DeviceDataMove::TakeUnreportedSkippedAddons()
    {
        return std::exchange(m_unreported, {});
    }
}
