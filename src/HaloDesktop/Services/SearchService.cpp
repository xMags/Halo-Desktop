#include "pch.h"
#include "Services/SearchService.h"

#include "Api/BackendSelector.h"
#include "Api/HaloBackend.h"
#include "Api/CatalogExtrasPolicy.h"
#include "Services/AddonService.h"
#include "Services/DevicePreferencesStore.h"
#include "Services/LibraryService.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace
{
    std::optional<std::wstring> Text(std::optional<winrt::hstring> const& value)
    {
        return value ? std::optional<std::wstring>{ std::wstring{ *value } } : std::nullopt;
    }

    std::wstring TargetKey(
        winrt::hstring const& addonId,
        winrt::hstring const& type,
        winrt::hstring const& catalogId)
    {
        // Unit separators: none of the parts can contain one, the mappers
        // having stripped control characters from every display string.
        std::wstring key{ addonId };
        key.push_back(L'\x1F');
        key.append(type);
        key.push_back(L'\x1F');
        key.append(catalogId);
        return key;
    }

    // The effective resolution order, as Home reads it: global addons first,
    // then the user's own minus those the administrator already installs.
    // Hidden catalogs are left out, as the server leaves them out of the
    // manifest the reference reads.
    bool InEffectiveOrder(::HaloDesktop::Api::Dto::AddonRecord const& addon) noexcept
    {
        return !addon.HideCatalogs && !(!addon.IsGlobal && addon.ProvidedGlobally);
    }

    std::vector<HaloDesktop::Services::SearchTitle> Titles(
        std::vector<::HaloDesktop::Api::Dto::MetaPreview> const& previews)
    {
        std::vector<HaloDesktop::Services::SearchTitle> titles;
        titles.reserve(previews.size());
        for (auto const& preview : previews)
        {
            titles.push_back(HaloDesktop::Services::ToSearchTitle(preview));
        }
        return HaloDesktop::Services::UniqueTitles(std::move(titles));
    }

    std::int64_t NowMilliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    template <typename Task>
    void ObserveDetached(Task&& task)
    {
        // Not awaited: what it does lands through the service's own state.
        // The continuation exists only so a failure cannot surface as an
        // unobserved task exception.
        std::forward<Task>(task).then([](concurrency::task<void> completed)
        {
            try
            {
                completed.get();
            }
            catch (...)
            {
            }
        });
    }
}

namespace HaloDesktop::Services
{
    SearchTitle ToSearchTitle(::HaloDesktop::Api::Dto::MetaPreview const& preview)
    {
        return {
            std::wstring{ preview.Id },
            std::wstring{ preview.Type },
            std::wstring{ preview.Name },
            Text(preview.Poster),
            Text(preview.Background),
            Text(preview.Description),
            Text(preview.ReleaseInfo),
            Text(preview.Rating),
            Text(preview.Logo),
            Text(preview.DefaultVideoId),
            Text(preview.ImdbId),
        };
    }

    SearchService::SearchService(
        std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
        std::shared_ptr<AddonService> addons,
        std::shared_ptr<LibraryService> library,
        std::shared_ptr<DevicePreferencesStore> preferences)
        : m_backends(std::move(backends)),
          m_addons(std::move(addons)),
          m_library(std::move(library)),
          m_preferences(std::move(preferences))
    {
        if (!m_backends || !m_addons || !m_library || !m_preferences)
        {
            throw std::invalid_argument{ "SearchService requires all dependencies." };
        }
        // An older build's plain list is read as terms of unknown age and is
        // rewritten in the new shape only when the history next changes.
        m_history = m_preferences->SearchHistory();
    }

    void SearchService::Search(std::wstring const& query, bool force)
    {
        auto term = TrimmedText(query);
        if (!IsSearchableQuery(term))
        {
            term.clear();
        }
        if (!force && term == m_term && (m_phase != SearchPhase::Idle || term.empty()))
        {
            return;
        }

        // Every ticket still out belongs to the old board and can no longer
        // match a slot: the board's generations only ever rise.
        ++m_serial;
        m_board.Reset();
        m_targets.clear();
        m_targetRanks.clear();
        m_answers.clear();
        m_term = std::move(term);
        if (m_term.empty())
        {
            m_phase = SearchPhase::Idle;
            NotifyChanged();
            return;
        }
        m_phase = SearchPhase::LoadingAddons;
        NotifyChanged();
        ObserveDetached(StartAsync(m_serial));
    }

    concurrency::task<void> SearchService::StartAsync(std::uint64_t serial)
    {
        auto const weak = weak_from_this();
        auto const uiContext = winrt::apartment_context{};
        auto const addons = m_addons;
        auto failed = false;
        try
        {
            co_await addons->LoadAsync();
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;
        auto const self = weak.lock();
        if (!self || serial != self->m_serial)
        {
            co_return;
        }
        if (failed)
        {
            self->m_phase = SearchPhase::AddonsFailed;
            self->NotifyChanged();
            co_return;
        }
        self->AskTargets();
    }

    void SearchService::AskTargets()
    {
        std::vector<SearchTarget> targets;
        std::vector<std::size_t> ranks;
        std::unordered_set<std::wstring> keys;
        // The addon's place among those in the effective order.
        std::size_t addonRank = 0;
        for (auto const& addon : m_addons->Records())
        {
            if (!InEffectiveOrder(addon))
            {
                continue;
            }
            auto const rank = addonRank++;
            for (auto const& catalog : addon.Catalogs)
            {
                // Only the query is sent, so a catalog that insists on another
                // extra cannot answer and would only be reported as failing.
                // Only films and series are searched: TV channels and debrid
                // file lists answer with entries no title page can open.
                if (!catalog.SupportsSearch
                    || !::HaloDesktop::Api::SearchNeedsNothingElse(catalog)
                    || !IsSearchedCatalogType(std::wstring_view{ catalog.Type }))
                {
                    continue;
                }
                auto key = TargetKey(addon.Id, catalog.Type, catalog.Id);
                if (!keys.insert(key).second)
                {
                    continue;
                }
                targets.push_back({
                    std::move(key),
                    std::wstring{ addon.Id },
                    std::wstring{ addon.Name },
                    std::wstring{ catalog.Type },
                    std::wstring{ catalog.Id },
                    std::wstring{ catalog.Name.value_or(addon.Name) },
                });
                ranks.push_back(rank);
            }
        }

        std::vector<std::wstring> planKeys;
        planKeys.reserve(targets.size());
        for (auto const& target : targets)
        {
            planKeys.push_back(target.Key);
        }
        auto tickets = m_board.Plan(planKeys, HomeShelfRefetch::RefetchAll);
        m_targets = std::move(targets);
        m_targetRanks = std::move(ranks);
        m_phase = SearchPhase::Asking;

        auto const weak = weak_from_this();
        auto const uiContext = winrt::apartment_context{};
        auto const accountVersion = m_accountVersion;
        winrt::hstring const term{ m_term };
        for (auto& ticket : tickets)
        {
            auto const target = std::find_if(m_targets.begin(), m_targets.end(), [&ticket](SearchTarget const& candidate)
            {
                return candidate.Key == ticket.Key;
            });
            if (target == m_targets.end())
            {
                continue;
            }
            // Asked through whichever backend is current when it runs, so the
            // second try of a failed catalog goes where the first would now.
            TargetAsk ask = [backends = m_backends, addonId = winrt::hstring{ target->AddonId },
                type = winrt::hstring{ target->Type }, catalogId = winrt::hstring{ target->Id }, term]()
            {
                return backends->Current()->GetCatalogAsync(addonId, type, catalogId, { { L"search", term } });
            };
            std::optional<concurrency::task<std::vector<::HaloDesktop::Api::Dto::MetaPreview>>> request;
            try
            {
                request = ask();
            }
            catch (...)
            {
            }
            if (!request)
            {
                ApplyTargetAnswer(ticket, std::nullopt);
                continue;
            }
            ObserveDetached(RunTargetFetchAsync(
                weak, uiContext, std::move(*request), std::move(ask), std::move(ticket), accountVersion, m_serial));
        }
        // After the requests are out, so a handler that searches again from
        // inside this cannot leave this query's loop half started.
        NotifyChanged();
    }

    concurrency::task<void> SearchService::RunTargetFetchAsync(
        std::weak_ptr<SearchService> weak,
        winrt::apartment_context uiContext,
        concurrency::task<std::vector<::HaloDesktop::Api::Dto::MetaPreview>> request,
        TargetAsk again,
        HomeShelfTicket ticket,
        std::uint64_t accountVersion,
        std::uint64_t serial)
    {
        std::optional<std::vector<::HaloDesktop::Api::Dto::MetaPreview>> answer;
        try
        {
            answer = co_await request;
        }
        catch (...)
        {
        }
        co_await uiContext;
        if (!answer)
        {
            // One more try before the catalog is reported as not answering: a
            // slow addon often answers the second time. Only for the query
            // still on screen; an older one is not worth a request.
            std::optional<concurrency::task<std::vector<::HaloDesktop::Api::Dto::MetaPreview>>> second;
            {
                auto const self = weak.lock();
                if (!self || accountVersion != self->m_accountVersion || serial != self->m_serial)
                {
                    co_return;
                }
                try
                {
                    second = again();
                }
                catch (...)
                {
                }
            }
            if (second)
            {
                try
                {
                    answer = co_await *second;
                }
                catch (...)
                {
                }
                co_await uiContext;
            }
        }
        auto const self = weak.lock();
        if (!self || accountVersion != self->m_accountVersion)
        {
            co_return;
        }
        self->ApplyTargetAnswer(ticket, std::move(answer));
    }

    void SearchService::ApplyTargetAnswer(
        HomeShelfTicket const& ticket,
        std::optional<std::vector<::HaloDesktop::Api::Dto::MetaPreview>> answer)
    {
        auto const kind = !answer
            ? HomeShelfAnswer::Failed
            : answer->empty() ? HomeShelfAnswer::Empty : HomeShelfAnswer::Items;
        // Discarded for an older query's ticket. A fresh board has nothing
        // shown, so a failure is always applied rather than kept over titles.
        if (m_board.Complete(ticket, kind) != HomeShelfApply::Applied)
        {
            return;
        }
        if (kind == HomeShelfAnswer::Items)
        {
            m_answers.insert_or_assign(ticket.Key, Titles(*answer));
        }
        else
        {
            m_answers.erase(ticket.Key);
        }
        NotifyChanged();
    }

    SearchPhase SearchService::Phase() const noexcept { return m_phase; }
    std::wstring const& SearchService::Term() const noexcept { return m_term; }
    std::vector<SearchTarget> const& SearchService::Targets() const noexcept { return m_targets; }
    std::vector<std::size_t> const& SearchService::TargetAddonRanks() const noexcept { return m_targetRanks; }
    std::uint64_t SearchService::Serial() const noexcept { return m_serial; }

    std::vector<SearchTargetState> SearchService::States() const
    {
        std::vector<SearchTargetState> states;
        states.reserve(m_targets.size());
        for (auto const& target : m_targets)
        {
            SearchTargetState state;
            if (auto const index = m_board.IndexOf(target.Key))
            {
                switch (m_board.Slots()[*index].State)
                {
                case HomeShelfState::Loading:
                    state.Status = SearchTargetStatus::Pending;
                    break;
                case HomeShelfState::Failed:
                    state.Status = SearchTargetStatus::Failed;
                    break;
                case HomeShelfState::Loaded:
                case HomeShelfState::Empty:
                    state.Status = SearchTargetStatus::Done;
                    if (auto const found = m_answers.find(target.Key); found != m_answers.end())
                    {
                        state.Titles = found->second;
                    }
                    break;
                }
            }
            states.push_back(std::move(state));
        }
        return states;
    }

    std::vector<SearchLibraryEntry> SearchService::LibraryEntries() const
    {
        std::vector<SearchLibraryEntry> entries;
        for (auto const& row : m_library->Rows())
        {
            entries.push_back({
                std::wstring{ row.Id },
                std::wstring{ row.Type },
                std::wstring{ row.Name },
                Text(row.Poster),
                row.AddedAt,
                row.RemovedAt.has_value(),
            });
        }
        return entries;
    }

    bool SearchService::InLibrary(std::wstring const& type, std::wstring const& id) const
    {
        auto const itemId = type + L':' + id;
        auto const rows = m_library->Rows();
        return std::any_of(rows.begin(), rows.end(), [&itemId](::HaloDesktop::Api::Dto::LibraryRow const& row)
        {
            return std::wstring_view{ row.Id } == itemId && !row.RemovedAt;
        });
    }

    std::vector<SearchHistoryEntry> const& SearchService::History() const noexcept { return m_history; }

    void SearchService::RecordTerm(std::wstring_view term)
    {
        auto next = AddSearchTerm(m_history, term, NowMilliseconds());
        if (next == m_history)
        {
            return;
        }
        m_history = std::move(next);
        SaveHistory();
        NotifyChanged();
    }

    void SearchService::RemoveTerm(std::wstring_view term)
    {
        auto next = RemoveSearchTerm(m_history, term);
        if (next.size() == m_history.size())
        {
            return;
        }
        m_history = std::move(next);
        SaveHistory();
        NotifyChanged();
    }

    void SearchService::ClearHistory()
    {
        if (m_history.empty())
        {
            return;
        }
        m_history.clear();
        SaveHistory();
        NotifyChanged();
    }

    void SearchService::SaveHistory()
    {
        // Best effort, as the reference's: the list in memory stays right for
        // this session even if the file cannot be written.
        try
        {
            m_preferences->SearchHistory(m_history);
        }
        catch (...)
        {
        }
    }

    std::uint64_t SearchService::AccountVersion() const noexcept { return m_accountVersion; }

    SearchChangedToken SearchService::AddChangedHandler(SearchChangedHandler handler)
    {
        if (!handler)
        {
            return 0;
        }
        auto const token = m_nextToken++;
        m_handlers.emplace(token, std::move(handler));
        return token;
    }

    void SearchService::RemoveChangedHandler(SearchChangedToken token) noexcept
    {
        m_handlers.erase(token);
    }

    void SearchService::NotifyChanged()
    {
        // Copied first: a handler is free to unsubscribe, or to search again,
        // from inside itself.
        std::vector<SearchChangedHandler> handlers;
        handlers.reserve(m_handlers.size());
        for (auto const& entry : m_handlers)
        {
            handlers.push_back(entry.second);
        }
        for (auto const& handler : handlers)
        {
            try
            {
                handler();
            }
            catch (...)
            {
            }
        }
    }

    void SearchService::OnAccountChanged()
    {
        ++m_accountVersion;
        ++m_serial;
        m_board.Reset();
        m_targets.clear();
        m_targetRanks.clear();
        m_answers.clear();
        m_term.clear();
        m_phase = SearchPhase::Idle;
        // The history is the device's, not the account's, and stays.
        NotifyChanged();
    }
}
