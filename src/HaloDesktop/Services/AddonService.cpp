#include "pch.h"
#include "Services/AddonService.h"

#include "Api/ApiClient.h"
#include "Api/BackendSelector.h"
#include "Api/HaloBackend.h"
#include "Models/Models.h"
#include "Services/QueryCache.h"

#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr wchar_t AddonsCacheKey[] = L"addons";

    winrt::hstring Trimmed(winrt::hstring const& input)
    {
        std::wstring value{ input };
        auto const first = std::find_if_not(value.begin(), value.end(), [](wchar_t character)
        {
            return std::iswspace(character) != 0;
        });
        auto const last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t character)
        {
            return std::iswspace(character) != 0;
        }).base();
        return first < last ? winrt::hstring{ std::wstring(first, last) } : winrt::hstring{};
    }

    winrt::hstring Initials(winrt::hstring const& name)
    {
        std::wstring result;
        auto newWord = true;
        for (auto const character : std::wstring_view{ name.c_str(), name.size() })
        {
            if (std::iswspace(character) != 0)
            {
                newWord = true;
                continue;
            }
            if (newWord)
            {
                result.push_back(static_cast<wchar_t>(std::towupper(character)));
                if (result.size() == 2)
                {
                    break;
                }
                newWord = false;
            }
        }
        if (result.empty())
        {
            result = L"AD";
        }
        return winrt::hstring{ result };
    }

    winrt::hstring Provides(::HaloDesktop::Api::Dto::AddonRecord const& addon)
    {
        std::wstring result;
        auto append = [&result](winrt::hstring const& value)
        {
            if (!result.empty())
            {
                result.append(L" · ");
            }
            result.append(value);
        };
        for (auto const& resource : addon.Resources)
        {
            append(resource);
        }
        if (result.empty())
        {
            for (auto const& type : addon.Types)
            {
                append(type);
            }
        }
        return result.empty() ? winrt::hstring{ L"Addon services" } : winrt::hstring{ result };
    }
}

namespace HaloDesktop::Services
{
    AddonService::AddonService(
        std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
        std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
        std::shared_ptr<QueryCache> queryCache,
        std::shared_ptr<ISessionService> session)
        : m_apiClient(std::move(apiClient)),
          m_backends(std::move(backends)),
          m_queryCache(std::move(queryCache)),
          m_session(std::move(session)),
          m_items(winrt::single_threaded_observable_vector<winrt::HaloDesktop::Addon>())
    {
        if (!m_apiClient || !m_backends || !m_queryCache || !m_session)
        {
            throw std::invalid_argument{ "AddonService requires all dependencies." };
        }
        // The admin flag can change after the addons were applied, as when
        // /auth/me answers after them. `this` is sound: the session outlives
        // this service (it is held here), the destructor removes the handler,
        // and both live on the UI thread, which is the only one that raises it.
        m_profileChangedToken = m_session->AddProfileChangedHandler([this]() { RefreshEditability(); });
    }

    AddonService::~AddonService()
    {
        if (m_profileChangedToken != 0)
        {
            m_session->RemoveProfileChangedHandler(m_profileChangedToken);
        }
    }

    void AddonService::RefreshEditability()
    {
        // Before this account's list has been read there are no rows to judge,
        // and an empty list must not read as one that can be saved over.
        if (m_listApplied && m_session->IsAdmin() != m_builtAsAdmin)
        {
            RebuildItems();
        }
    }

    winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::Addon> AddonService::Items() const
    {
        return m_items;
    }

    std::vector<::HaloDesktop::Api::Dto::AddonRecord> AddonService::Records() const
    {
        return m_records;
    }

    bool AddonService::CanEditLists() const noexcept
    {
        return m_editability.User;
    }

    concurrency::task<void> AddonService::LoadAsync()
    {
        // Home, Search, and Settings can request addons at the same time. Every
        // caller must await the one snapshot that will actually be applied.
        auto const accountVersion = m_accountVersion;
        if (m_loadTask && m_loadTaskAccountVersion == accountVersion)
        {
            auto task = *m_loadTask;
            co_await task;
            co_return;
        }

        auto task = LoadCoreAsync(accountVersion);
        m_loadTask = task;
        m_loadTaskAccountVersion = accountVersion;
        try
        {
            co_await task;
        }
        catch (...)
        {
            if (accountVersion == m_accountVersion)
            {
                m_loadTask.reset();
            }
            throw;
        }
        if (accountVersion == m_accountVersion)
        {
            m_loadTask.reset();
        }
    }

    concurrency::task<void> AddonService::LoadCoreAsync(std::uint64_t accountVersion)
    {
        auto const uiContext = winrt::apartment_context{};
        if (auto const cached = m_queryCache->TryGet<::HaloDesktop::Api::Dto::AddonsPayload>(AddonsCacheKey))
        {
            Apply(*cached);
            co_return;
        }

        auto const requestId = m_queryCache->Issue(AddonsCacheKey);
        auto payload = co_await m_backends->Current()->GetAddonsAsync();
        co_await uiContext;
        if (accountVersion != m_accountVersion)
        {
            co_return;
        }
        // Without an account the built-ins are topped up by the session
        // (TopUpDeviceAddonsAsync), which never seeds over the PC's own list;
        // seeding here as well would only race it. The same holds while this
        // PC's data is still moving into the account: the move seeds first and
        // then adds this PC's addons one by one, and a seed from here landing
        // between two of its writes would replace the list and lose them. The
        // move reloads account data when it ends, which seeds here if needed.
        auto const moveOwnsSeeding = m_session->HasDeviceDataToMove();
        auto const shouldSeed = !m_seedAttempted && payload.Global.empty() && payload.User.empty()
            && m_session->Mode() != SessionMode::Device && !moveOwnsSeeding;
        if (!moveOwnsSeeding)
        {
            m_seedAttempted = true;
        }
        if (shouldSeed)
        {
            std::vector<winrt::hstring> defaults;
            defaults.reserve(DefaultAddonUrls.size());
            for (auto const url : DefaultAddonUrls)
            {
                defaults.emplace_back(url);
            }
            // Best effort, as the reference client seeds: the server validates
            // every manifest, so one built-in host being down refuses the whole
            // list, and that must not fail the first load. The empty list
            // already read is applied instead.
            std::optional<::HaloDesktop::Api::Dto::AddonsPayload> seeded;
            try
            {
                co_await m_backends->Current()->PutAddonsAsync(std::move(defaults));
                seeded = co_await m_backends->Current()->GetAddonsAsync();
            }
            catch (winrt::hresult_canceled const&)
            {
                throw;
            }
            catch (...)
            {
            }
            co_await uiContext;
            if (accountVersion != m_accountVersion)
            {
                co_return;
            }
            if (seeded)
            {
                payload = std::move(*seeded);
            }
        }
        if (accountVersion == m_accountVersion
            && m_queryCache->Commit(AddonsCacheKey, requestId, payload, QueryTtl::Addons))
        {
            Apply(std::move(payload));
        }
    }

    concurrency::task<void> AddonService::AddAsync(winrt::hstring transportUrl)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const accountVersion = m_accountVersion;
        auto const previousMutation = m_mutationTail;
        concurrency::task_completion_event<void> mutationCompleted;
        auto const mutationGate = concurrency::create_task(mutationCompleted);
        m_mutationTail = mutationGate.then([](concurrency::task<void> completed)
        {
            try { completed.get(); } catch (...) {}
        });
        try
        {
            if (previousMutation)
            {
                auto pendingMutation = *previousMutation;
                try { co_await pendingMutation; } catch (...) {}
            }

            co_await uiContext;
            if (accountVersion != m_accountVersion)
            {
                mutationCompleted.set();
                co_return;
            }

            auto const url = Trimmed(transportUrl);
            if (!m_editability.User)
            {
                throw std::runtime_error{ "The addon list cannot be edited safely." };
            }
            winrt::Windows::Foundation::Uri uri{ url };
            if (url.empty() || uri.Host().empty()
                || (uri.SchemeName() != L"http" && uri.SchemeName() != L"https"))
            {
                throw std::invalid_argument{ "Enter a valid addon URL." };
            }
            for (auto const& record : m_records)
            {
                if (record.TransportUrl && *record.TransportUrl == url)
                {
                    throw std::invalid_argument{ "That addon is already installed." };
                }
            }

            auto urls = TransportUrls(false);
            urls.push_back(url);
            co_await PutListAsync(std::move(urls), false);
            co_await uiContext;
            if (accountVersion != m_accountVersion)
            {
                mutationCompleted.set();
                co_return;
            }
            m_queryCache->Invalidate(AddonsCacheKey);
            co_await LoadCoreAsync(m_accountVersion);
            mutationCompleted.set();
        }
        catch (...)
        {
            mutationCompleted.set();
            throw;
        }
    }

    concurrency::task<void> AddonService::RemoveAsync(winrt::hstring addonId)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const accountVersion = m_accountVersion;
        auto const previousMutation = m_mutationTail;
        concurrency::task_completion_event<void> mutationCompleted;
        auto const mutationGate = concurrency::create_task(mutationCompleted);
        m_mutationTail = mutationGate.then([](concurrency::task<void> completed)
        {
            try { completed.get(); } catch (...) {}
        });
        try
        {
            if (previousMutation)
            {
                auto pendingMutation = *previousMutation;
                try { co_await pendingMutation; } catch (...) {}
            }

            co_await uiContext;
            if (accountVersion != m_accountVersion)
            {
                mutationCompleted.set();
                co_return;
            }

            auto const found = std::find_if(m_records.begin(), m_records.end(), [&addonId](auto const& record)
            {
                return record.Id == addonId;
            });
            if (found == m_records.end() || !found->TransportUrl
                || !CanEditAddonInScope(m_editability, found->IsGlobal))
            {
                throw std::runtime_error{ "The addon cannot be removed safely." };
            }
            // Settings offers no remove for a built-in in the own list; this
            // keeps any other caller from taking away what Halo browses with.
            if (IsBuiltInAddon(
                    found->IsGlobal ? AddonScope::Global : AddonScope::Yours,
                    std::wstring_view{ *found->TransportUrl }))
            {
                throw std::runtime_error{ "A built-in addon cannot be removed." };
            }

            auto urls = TransportUrls(found->IsGlobal);
            std::erase(urls, *found->TransportUrl);
            co_await PutListAsync(std::move(urls), found->IsGlobal);
            co_await uiContext;
            if (accountVersion != m_accountVersion)
            {
                mutationCompleted.set();
                co_return;
            }
            m_queryCache->Invalidate(AddonsCacheKey);
            co_await LoadCoreAsync(m_accountVersion);
            mutationCompleted.set();
        }
        catch (...)
        {
            mutationCompleted.set();
            throw;
        }
    }

    concurrency::task<void> AddonService::SetCatalogsVisibleAsync(
        winrt::hstring addonId,
        bool visible)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const accountVersion = m_accountVersion;
        auto const previousMutation = m_mutationTail;
        concurrency::task_completion_event<void> mutationCompleted;
        auto const mutationGate = concurrency::create_task(mutationCompleted);
        m_mutationTail = mutationGate.then([](concurrency::task<void> completed)
        {
            try { completed.get(); } catch (...) {}
        });
        try
        {
            if (previousMutation)
            {
                auto pendingMutation = *previousMutation;
                try { co_await pendingMutation; } catch (...) {}
            }

            co_await uiContext;
            if (accountVersion != m_accountVersion)
            {
                mutationCompleted.set();
                co_return;
            }

            auto const found = std::find_if(m_records.begin(), m_records.end(), [&addonId](auto const& record)
            {
                return record.Id == addonId;
            });
            if (found == m_records.end() || (found->IsGlobal && !m_session->IsAdmin()))
            {
                throw std::runtime_error{ "The addon cannot be changed." };
            }
            co_await PatchListEntryAsync(found->Id, found->IsGlobal, !visible);
            co_await uiContext;
            if (accountVersion != m_accountVersion)
            {
                mutationCompleted.set();
                co_return;
            }
            m_queryCache->Invalidate(AddonsCacheKey);
            co_await LoadCoreAsync(m_accountVersion);
            mutationCompleted.set();
        }
        catch (...)
        {
            mutationCompleted.set();
            throw;
        }
    }

    bool AddonService::HasLoaded() const noexcept
    {
        return m_listApplied;
    }

    bool AddonService::CanEditScope(bool global) const noexcept
    {
        return m_listApplied && CanEditAddonInScope(m_editability, global);
    }

    std::vector<AddonListEntry> AddonService::ListEntries(bool global) const
    {
        std::vector<AddonListEntry> entries;
        for (auto const& record : m_records)
        {
            if (record.IsGlobal != global)
            {
                continue;
            }
            entries.push_back(AddonListEntry{
                .Id = std::wstring{ record.Id },
                .TransportUrl = record.TransportUrl
                    ? std::optional<std::wstring>{ std::wstring{ *record.TransportUrl } }
                    : std::nullopt,
            });
        }
        return entries;
    }

    InstallForEveryonePlan AddonService::PlanInstall(winrt::hstring const& addonId) const
    {
        auto const global = ListEntries(true);
        auto const own = ListEntries(false);
        return PlanInstallForEveryone(global, own, std::wstring_view{ addonId }, m_session->IsAdmin());
    }

    InstallForEveryoneCheck AddonService::CheckInstallForEveryone(winrt::hstring const& addonId) const
    {
        if (!m_listApplied)
        {
            return InstallForEveryoneCheck::NotAllowed;
        }
        return PlanInstall(addonId).Check;
    }

    concurrency::task<void> AddonService::SaveOrderAsync(
        bool global,
        std::vector<winrt::hstring> orderedIds)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const accountVersion = m_accountVersion;
        auto const previousMutation = m_mutationTail;
        concurrency::task_completion_event<void> mutationCompleted;
        auto const mutationGate = concurrency::create_task(mutationCompleted);
        m_mutationTail = mutationGate.then([](concurrency::task<void> completed)
        {
            try { completed.get(); } catch (...) {}
        });
        try
        {
            if (previousMutation)
            {
                auto pendingMutation = *previousMutation;
                try { co_await pendingMutation; } catch (...) {}
            }

            co_await uiContext;
            if (accountVersion != m_accountVersion)
            {
                mutationCompleted.set();
                co_return;
            }

            if (!CanEditScope(global))
            {
                throw std::runtime_error{ "The addon list cannot be edited safely." };
            }
            std::vector<std::wstring> ids;
            ids.reserve(orderedIds.size());
            for (auto const& id : orderedIds)
            {
                ids.emplace_back(id);
            }
            auto const entries = ListEntries(global);
            auto const urls = ReorderedTransportUrls(entries, ids);
            if (!urls)
            {
                throw std::invalid_argument{ "The new addon order does not match the list." };
            }
            std::vector<winrt::hstring> body;
            body.reserve(urls->size());
            for (auto const& url : *urls)
            {
                body.emplace_back(url);
            }
            co_await PutListAsync(std::move(body), global);
            co_await uiContext;
            if (accountVersion != m_accountVersion)
            {
                mutationCompleted.set();
                co_return;
            }
            m_queryCache->Invalidate(AddonsCacheKey);
            co_await LoadCoreAsync(m_accountVersion);
            mutationCompleted.set();
        }
        catch (...)
        {
            mutationCompleted.set();
            throw;
        }
    }

    concurrency::task<InstallForEveryoneOutcome> AddonService::InstallForEveryoneAsync(winrt::hstring addonId)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const accountVersion = m_accountVersion;
        auto const previousMutation = m_mutationTail;
        concurrency::task_completion_event<void> mutationCompleted;
        auto const mutationGate = concurrency::create_task(mutationCompleted);
        m_mutationTail = mutationGate.then([](concurrency::task<void> completed)
        {
            try { completed.get(); } catch (...) {}
        });
        try
        {
            if (previousMutation)
            {
                auto pendingMutation = *previousMutation;
                try { co_await pendingMutation; } catch (...) {}
            }

            co_await uiContext;
            if (accountVersion != m_accountVersion)
            {
                mutationCompleted.set();
                co_return InstallForEveryoneOutcome::Abandoned;
            }

            // Planned again here, after any earlier save has landed, so the
            // lists sent are the ones the account has now.
            auto const plan = PlanInstall(addonId);
            if (!m_listApplied || plan.Check != InstallForEveryoneCheck::Proceed)
            {
                throw std::runtime_error{ "The addon cannot be installed for every user." };
            }
            std::vector<winrt::hstring> globalUrls;
            globalUrls.reserve(plan.GlobalUrls.size());
            for (auto const& url : plan.GlobalUrls)
            {
                globalUrls.emplace_back(url);
            }
            co_await PutListAsync(std::move(globalUrls), true);
            co_await uiContext;
            if (accountVersion != m_accountVersion)
            {
                mutationCompleted.set();
                co_return InstallForEveryoneOutcome::Abandoned;
            }
            m_queryCache->Invalidate(AddonsCacheKey);

            auto outcome = InstallForEveryoneOutcome::Installed;
            if (plan.OwnListIncomplete)
            {
                outcome = InstallForEveryoneOutcome::OwnCopyKept;
            }
            else if (plan.OwnUrlsAfter)
            {
                std::vector<winrt::hstring> ownUrls;
                ownUrls.reserve(plan.OwnUrlsAfter->size());
                for (auto const& url : *plan.OwnUrlsAfter)
                {
                    ownUrls.emplace_back(url);
                }
                try
                {
                    co_await PutListAsync(std::move(ownUrls), false);
                }
                catch (winrt::hresult_canceled const&)
                {
                    throw;
                }
                catch (...)
                {
                    outcome = InstallForEveryoneOutcome::OwnCopyKept;
                }
                co_await uiContext;
                if (accountVersion != m_accountVersion)
                {
                    mutationCompleted.set();
                    co_return InstallForEveryoneOutcome::Abandoned;
                }
                m_queryCache->Invalidate(AddonsCacheKey);
            }
            // The global copy is in place either way, so the list is read
            // again; a failed read leaves the old rows and is not this
            // action's failure.
            try
            {
                co_await LoadCoreAsync(m_accountVersion);
            }
            catch (winrt::hresult_canceled const&)
            {
                throw;
            }
            catch (...)
            {
            }
            mutationCompleted.set();
            co_return outcome;
        }
        catch (...)
        {
            mutationCompleted.set();
            throw;
        }
    }

    void AddonService::Apply(::HaloDesktop::Api::Dto::AddonsPayload payload)
    {
        m_records.clear();
        m_records.reserve(payload.Global.size() + payload.User.size());
        m_records.insert(m_records.end(), payload.Global.begin(), payload.Global.end());
        m_records.insert(m_records.end(), payload.User.begin(), payload.User.end());
        m_listApplied = true;
        RebuildItems();
    }

    void AddonService::RebuildItems()
    {
        std::vector<AddonEditInput> editInputs;
        editInputs.reserve(m_records.size());
        for (auto const& record : m_records)
        {
            editInputs.push_back({ record.IsGlobal, record.TransportUrl.has_value() });
        }
        m_builtAsAdmin = m_session->IsAdmin();
        m_editability = EvaluateAddonEditability(editInputs, m_builtAsAdmin);

        m_items.Clear();
        for (auto const& record : m_records)
        {
            auto const canEdit = CanEditAddonInScope(m_editability, record.IsGlobal);
            m_items.Append(winrt::make<winrt::HaloDesktop::implementation::Addon>(
                record.Id,
                record.TransportUrl.value_or(L""),
                Initials(record.Name),
                record.Name,
                record.Version,
                record.IsGlobal ? L"GLOBAL" : L"YOURS",
                Provides(record),
                canEdit,
                record.IsGlobal,
                !record.HideCatalogs));
        }
    }

    std::vector<winrt::hstring> AddonService::TransportUrls(bool global) const
    {
        std::vector<winrt::hstring> result;
        for (auto const& record : m_records)
        {
            if (record.IsGlobal == global)
            {
                if (!record.TransportUrl)
                {
                    throw std::runtime_error{ "The addon list cannot be edited safely." };
                }
                result.push_back(*record.TransportUrl);
            }
        }
        return result;
    }

    concurrency::task<void> AddonService::PutListAsync(std::vector<winrt::hstring> transportUrls, bool global)
    {
        if (global)
        {
            // The global list is the admin's, on the server; a device has none.
            if (m_backends->UsesDevice())
            {
                throw std::runtime_error{ "There is no global addon list without an account." };
            }
            return m_apiClient->PutAddonsAsync(std::move(transportUrls), true);
        }
        return m_backends->Current()->PutAddonsAsync(std::move(transportUrls));
    }

    concurrency::task<void> AddonService::PatchListEntryAsync(winrt::hstring addonId, bool global, bool hideCatalogs)
    {
        if (global)
        {
            if (m_backends->UsesDevice())
            {
                throw std::runtime_error{ "There is no global addon list without an account." };
            }
            return m_apiClient->PatchAddonAsync(std::move(addonId), true, hideCatalogs);
        }
        return m_backends->Current()->PatchAddonAsync(std::move(addonId), hideCatalogs);
    }

    void AddonService::OnAccountChanged()
    {
        ++m_accountVersion;
        m_loadTask.reset();
        m_mutationTail.reset();
        m_records.clear();
        m_items.Clear();
        m_editability = {};
        m_builtAsAdmin = false;
        m_listApplied = false;
        m_seedAttempted = false;
    }
}
