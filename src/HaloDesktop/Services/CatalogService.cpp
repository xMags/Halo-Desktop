#include "pch.h"
#include "Services/CatalogService.h"

#include "Api/BackendSelector.h"
#include "Api/HaloBackend.h"
#include "Api/CatalogExtrasPolicy.h"
#include "Models/Models.h"
#include "Services/AddonService.h"
#include "Services/CatalogLanguagePolicy.h"
#include "Services/ContinueArtworkService.h"
#include "Services/ContinueNextEpisodeService.h"
#include "Services/HistoryRowsPolicy.h"
#include "Services/HomeRowsPolicy.h"
#include "Services/LibraryService.h"
#include "Services/RatingPolicy.h"
#include "Services/SettingsSyncService.h"
#include "Services/SimilarTitlesPolicy.h"
#include "Services/WatchStateService.h"
#include "ViewModels/HomeStatePolicy.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
    std::wstring RequestKey(
        winrt::hstring const& addonId,
        winrt::hstring const& type,
        winrt::hstring const& catalogId,
        std::vector<std::pair<winrt::hstring, winrt::hstring>> const& extras)
    {
        // Unit separators: none of the parts can contain one, the mappers
        // having stripped control characters from every display string.
        std::wstring key{ addonId };
        for (auto const& part : { type, catalogId })
        {
            key.push_back(L'\x1F');
            key.append(part);
        }
        for (auto const& [name, value] : extras)
        {
            key.push_back(L'\x1F');
            key.append(name);
            key.push_back(L'=');
            key.append(value);
        }
        return key;
    }

    std::vector<::HaloDesktop::Services::CatalogExtraDeclaration> ExtraDeclarations(
        ::HaloDesktop::Api::Dto::AddonRecord::Catalog const& catalog)
    {
        std::vector<::HaloDesktop::Services::CatalogExtraDeclaration> declarations;
        declarations.reserve(catalog.Extras.size());
        for (auto const& extra : catalog.Extras)
        {
            ::HaloDesktop::Services::CatalogExtraDeclaration declaration{ std::wstring{ extra.Name }, extra.IsRequired, {} };
            declaration.Options.reserve(extra.Options.size());
            for (auto const& option : extra.Options)
            {
                declaration.Options.emplace_back(option);
            }
            declarations.push_back(std::move(declaration));
        }
        return declarations;
    }

    winrt::HaloDesktop::MediaKind Kind(winrt::hstring const& type)
    {
        return type == L"series" ? winrt::HaloDesktop::MediaKind::Series : winrt::HaloDesktop::MediaKind::Movie;
    }

    std::optional<std::wstring_view> RatingText(::HaloDesktop::Api::Dto::MetaPreview const& item)
    {
        return item.Rating ? std::optional<std::wstring_view>{ *item.Rating } : std::nullopt;
    }

    winrt::hstring MetaLine(::HaloDesktop::Api::Dto::MetaPreview const& item)
    {
        std::wstring result;
        if (item.ReleaseInfo) result.append(*item.ReleaseInfo);
        if (auto const rating = ::HaloDesktop::Services::RatingLabel(RatingText(item)))
        {
            if (!result.empty()) result.append(L" · ");
            result.append(*rating);
        }
        return winrt::hstring{ result };
    }

    winrt::HaloDesktop::MediaSummary MakeMedia(
        ::HaloDesktop::Api::Dto::MetaPreview const& item,
        std::int64_t addedAt = 0,
        std::int64_t updatedAt = 0)
    {
        return winrt::make<winrt::HaloDesktop::implementation::MediaSummary>(
            item.Id, item.Name, MetaLine(item), Kind(item.Type), item.Type,
            item.Poster.value_or(L""), item.Background.value_or(L""), item.Description.value_or(L""),
            winrt::hstring{ ::HaloDesktop::Services::RatedValue(RatingText(item)).value_or(L"") },
            item.ReleaseInfo.value_or(L""), addedAt, updatedAt,
            item.Logo.value_or(L""), item.DefaultVideoId.value_or(L""));
    }

    // The most cards the shelf shows, and the most lookups one fill will start.
    // A fill cannot usefully exceed the cards, since nothing beyond them can be
    // drawn however many series are waiting behind.
    constexpr std::size_t MaximumContinueCards = 8;
    constexpr std::size_t MaximumContinueRequests = MaximumContinueCards;

    // The reference continue card prints `formatClock(remaining) LEFT`, not the
    // title page's `formatTimeLeft` wording, so this stays a clock.
    winrt::hstring TimeLeft(double duration, double position)
    {
        auto seconds = static_cast<std::int64_t>((std::max)(0.0, duration - position));
        auto const hours = seconds / 3600;
        seconds %= 3600;
        auto const minutes = seconds / 60;
        auto const remainder = seconds % 60;
        std::wostringstream output;
        if (hours > 0) output << hours << L':' << std::setw(2) << std::setfill(L'0') << minutes << L':' << std::setw(2) << remainder;
        else output << std::setw(2) << std::setfill(L'0') << minutes << L':' << std::setw(2) << remainder;
        output << L" LEFT";
        return winrt::hstring{ output.str() };
    }
}

namespace HaloDesktop::Services
{
    CatalogService::CatalogService(
        std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
        std::shared_ptr<AddonService> addons,
        std::shared_ptr<LibraryService> library,
        std::shared_ptr<WatchStateService> watchState,
        std::shared_ptr<ContinueArtworkService> artwork,
        std::shared_ptr<ContinueNextEpisodeService> nextEpisodes,
        std::shared_ptr<SettingsSyncService> settings)
        : m_backends(std::move(backends)),
          m_addons(std::move(addons)),
          m_library(std::move(library)),
          m_watchState(std::move(watchState)),
          m_artwork(std::move(artwork)),
          m_nextEpisodes(std::move(nextEpisodes)),
          m_settings(std::move(settings))
    {
        if (!m_backends || !m_addons || !m_library || !m_watchState || !m_artwork ||
            !m_nextEpisodes || !m_settings)
        {
            throw std::invalid_argument{ "CatalogService requires all dependencies." };
        }
        m_continue = winrt::single_threaded_vector<winrt::HaloDesktop::ContinueItem>().GetView();
        m_catalogShelves = winrt::single_threaded_vector<winrt::HaloDesktop::Shelf>().GetView();
        m_shelves = winrt::single_threaded_vector<winrt::HaloDesktop::Shelf>().GetView();
        m_libraryItems = winrt::single_threaded_vector<winrt::HaloDesktop::MediaSummary>().GetView();
        // Raw capture: the destructor always removes it, and the watch-state
        // service only ever calls back on this thread.
        m_watchChangedToken = m_watchState->AddChangedHandler([this]() { OnWatchRowsChanged(); });
    }

    CatalogService::~CatalogService()
    {
        if (m_watchChangedToken != 0)
        {
            m_watchState->RemoveChangedHandler(m_watchChangedToken);
        }
    }

    void CatalogService::OnWatchRowsChanged()
    {
        // Before Home's first load nothing has been drawn from the rows, and a
        // refresh in flight publishes them itself as its watch read lands.
        if (!m_refreshState.HasLoaded() || m_loadTask)
        {
            return;
        }
        PublishUserStateChange();
    }

    concurrency::task<void> CatalogService::LoadAsync()
    {
        if (m_refreshState.HasLoaded())
        {
            co_return;
        }
        co_await RunSharedRefreshAsync(true);
    }

    concurrency::task<void> CatalogService::RefreshCatalogsIfDirtyAsync()
    {
        if (!m_refreshState.HasLoaded())
        {
            co_await RunSharedRefreshAsync(true);
            co_return;
        }
        if (!CatalogsDirty())
        {
            co_return;
        }
        co_await RunSharedRefreshAsync(false);
    }

    concurrency::task<void> CatalogService::RunSharedRefreshAsync(bool loadDependencies)
    {
        // Shell and Home can arrive together, and a dirty Home can be entered more
        // than once while its request is outstanding. Every caller awaits the one
        // candidate that can commit.
        auto const accountVersion = m_accountVersion;
        if (m_loadTask && m_loadTaskAccountVersion == accountVersion)
        {
            auto task = *m_loadTask;
            co_await task;
            co_return;
        }

        auto task = LoadCoreAsync(loadDependencies, accountVersion);
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

    concurrency::task<void> CatalogService::LoadCoreAsync(
        bool loadDependencies,
        std::uint64_t accountVersion)
    {
        auto const ticket = m_refreshState.TryBeginRefresh();
        if (!ticket)
        {
            throw std::logic_error{ "A catalog refresh is already running." };
        }

        auto const uiContext = winrt::apartment_context{};
        // Only a refresh the catalog language alone asked for leaves the shelves
        // whose request is unchanged alone; any other change asks them all again.
        auto const refetch = m_refreshState.HasLoaded() && !m_refreshState.IsDirty()
            ? HomeShelfRefetch::KeepAnswered
            : HomeShelfRefetch::RefetchAll;
        try
        {
            if (loadDependencies)
            {
                // All four at once. The library and the watch history are the
                // user's own rows and must not wait on any addon; the settings
                // carry the language the shelves are asked with.
                auto settingsLoad = m_settings->LoadAsync();
                auto addonsLoad = m_addons->LoadAsync();
                auto libraryLoad = m_library->LoadAsync();
                auto watchLoad = m_watchState->LoadAsync();

                // Every task started above is awaited whatever happens, so none
                // can end with an exception nobody observed.
                std::exception_ptr failure;
                try
                {
                    co_await libraryLoad;
                }
                catch (...)
                {
                    failure = std::current_exception();
                }
                try
                {
                    co_await watchLoad;
                }
                catch (...)
                {
                    if (!failure) failure = std::current_exception();
                }
                co_await uiContext;
                if (!failure && accountVersion == m_accountVersion)
                {
                    PublishUserRows();
                    ++m_snapshotVersion;
                    NotifyHomeChanged();
                }

                try
                {
                    co_await addonsLoad;
                }
                catch (...)
                {
                    if (!failure) failure = std::current_exception();
                }
                try
                {
                    co_await settingsLoad;
                }
                catch (...)
                {
                    // Without settings the language catalogs keep their default.
                }
                co_await uiContext;
                if (accountVersion != m_accountVersion)
                {
                    co_return;
                }
                if (failure)
                {
                    std::rethrow_exception(failure);
                }
            }

            // From here to the end there is no await: the shelf list, the user's
            // rows and the refresh state change together, and the requests start
            // only once all of it is in place.
            auto const language = CurrentCatalogLanguage();
            auto queries = BuildShelfQueries(language
                ? std::optional<std::wstring_view>{ *language }
                : std::nullopt);
            std::vector<std::wstring> keys;
            keys.reserve(queries.size());
            for (auto const& query : queries)
            {
                keys.push_back(query.Key);
            }
            auto tickets = m_shelfBoard.Plan(keys, refetch);
            m_shelfQueries = std::move(queries);
            std::erase_if(m_shelfAnswers, [this](auto const& entry) { return !m_shelfBoard.IndexOf(entry.first); });
            std::erase_if(m_builtShelves, [this](auto const& entry) { return !m_shelfBoard.IndexOf(entry.first); });
            m_appliedLanguage = language;
            PublishUserRows();
            ++m_snapshotVersion;
            m_refreshState.CompleteRefresh(*ticket, true);
            NotifyHomeChanged();
            StartShelfFetches(std::move(tickets));
        }
        catch (...)
        {
            if (accountVersion == m_accountVersion)
            {
                m_refreshState.CompleteRefresh(*ticket, false);
            }
            throw;
        }
    }

    std::vector<CatalogService::ShelfQuery> CatalogService::BuildShelfQueries(
        std::optional<std::wstring_view> language) const
    {
        std::vector<ShelfQuery> queries;
        std::unordered_set<std::wstring> keys;
        // The effective resolution order: global addons first, then the user's
        // own minus those the administrator already installs.
        for (auto const& addon : m_addons->Records())
        {
            if (queries.size() == MaximumHomeShelves)
            {
                break;
            }
            if (addon.HideCatalogs || (!addon.IsGlobal && addon.ProvidedGlobally))
            {
                continue;
            }
            for (auto const& catalog : addon.Catalogs)
            {
                if (queries.size() == MaximumHomeShelves)
                {
                    break;
                }
                if (catalog.HasRequiredExtra || !IsHomeCatalogType(std::wstring_view{ catalog.Type }))
                {
                    continue;
                }
                std::vector<std::pair<winrt::hstring, winrt::hstring>> extras;
                auto const declarations = ExtraDeclarations(catalog);
                if (auto const genre = CatalogLanguageGenre(declarations, language))
                {
                    extras.emplace_back(L"genre", winrt::hstring{ *genre });
                }
                auto key = RequestKey(addon.Id, catalog.Type, catalog.Id, extras);
                // The same request twice is one shelf: it can only ever show
                // one answer, and the board tracks it by this key.
                if (!keys.insert(key).second)
                {
                    continue;
                }
                queries.push_back({ addon.Id, addon.Name, catalog, std::move(extras), std::move(key) });
            }
        }
        return queries;
    }

    void CatalogService::StartShelfFetches(std::vector<HomeShelfTicket> tickets)
    {
        auto const weak = weak_from_this();
        auto const uiContext = winrt::apartment_context{};
        auto const accountVersion = m_accountVersion;
        for (auto& ticket : tickets)
        {
            auto const query = std::find_if(m_shelfQueries.begin(), m_shelfQueries.end(), [&ticket](ShelfQuery const& candidate)
            {
                return candidate.Key == ticket.Key;
            });
            if (query == m_shelfQueries.end())
            {
                continue;
            }
            std::optional<concurrency::task<std::vector<::HaloDesktop::Api::Dto::MetaPreview>>> request;
            try
            {
                request = m_backends->Current()->GetCatalogAsync(query->AddonId, query->Catalog.Type, query->Catalog.Id, query->Extras);
            }
            catch (...)
            {
            }
            if (!request)
            {
                ApplyShelfAnswer(ticket, std::nullopt);
                continue;
            }
            // Not awaited: the shelf list is already published, and each shelf
            // lands when its own catalog answers. The continuation exists only
            // so a failure cannot surface as an unobserved task exception.
            RunShelfFetchAsync(weak, uiContext, std::move(*request), std::move(ticket), accountVersion)
                .then([](concurrency::task<void> completed)
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

    concurrency::task<void> CatalogService::RunShelfFetchAsync(
        std::weak_ptr<CatalogService> weak,
        winrt::apartment_context uiContext,
        concurrency::task<std::vector<::HaloDesktop::Api::Dto::MetaPreview>> request,
        HomeShelfTicket ticket,
        std::uint64_t accountVersion)
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
        auto const self = weak.lock();
        if (!self || accountVersion != self->m_accountVersion)
        {
            co_return;
        }
        self->ApplyShelfAnswer(ticket, std::move(answer));
    }

    void CatalogService::ApplyShelfAnswer(
        HomeShelfTicket const& ticket,
        std::optional<std::vector<::HaloDesktop::Api::Dto::MetaPreview>> answer)
    {
        auto const kind = !answer
            ? HomeShelfAnswer::Failed
            : answer->empty() ? HomeShelfAnswer::Empty : HomeShelfAnswer::Items;
        auto const applied = m_shelfBoard.Complete(ticket, kind);
        if (applied == HomeShelfApply::Discarded)
        {
            return;
        }
        if (applied == HomeShelfApply::Applied)
        {
            m_builtShelves.erase(ticket.Key);
            if (kind == HomeShelfAnswer::Items)
            {
                m_shelfAnswers.insert_or_assign(ticket.Key, std::move(*answer));
            }
            else
            {
                m_shelfAnswers.erase(ticket.Key);
            }
            ComposeCatalogShelves();
        }
        if (m_shelfBoard.Settled())
        {
            auto const slots = m_shelfBoard.Slots();
            auto const answered = static_cast<std::size_t>(std::count_if(slots.begin(), slots.end(), [](HomeShelfSlot const& slot)
            {
                return !slot.LastFailed;
            }));
            // A refresh every catalog failed stays due, so the next visit or a
            // retry asks again, while whatever was shown before stays shown.
            if (DecideCatalogCommit(slots.size(), answered) == CatalogCommitDecision::PreserveAndFail)
            {
                m_refreshState.Invalidate();
            }
        }
        ++m_snapshotVersion;
        NotifyHomeChanged();
    }

    winrt::HaloDesktop::Shelf CatalogService::BuildCatalogShelf(
        ShelfQuery const& query,
        std::vector<::HaloDesktop::Api::Dto::MetaPreview> const* answer) const
    {
        auto const name = query.Catalog.Name
            ? std::optional<std::wstring_view>{ std::wstring_view{ *query.Catalog.Name } }
            : std::nullopt;
        auto labels = CatalogShelfLabels(name, std::wstring_view{ query.Catalog.Type }, std::wstring_view{ query.AddonName });

        std::vector<winrt::HaloDesktop::MediaSummary> items;
        std::shared_ptr<CatalogPagingSource const> paging;
        if (answer)
        {
            items.reserve(answer->size());
            for (auto const& item : *answer)
            {
                items.push_back(MakeMedia(item));
            }

            // A catalog that pages carries what its grid needs to go on: the
            // request and the first page as the addon sent it.
            if (::HaloDesktop::Api::SupportsSkip(query.Catalog))
            {
                auto source = std::make_shared<CatalogPagingSource>();
                source->AddonId = query.AddonId;
                source->Type = query.Catalog.Type;
                source->CatalogId = query.Catalog.Id;
                for (auto const& [extraName, value] : query.Extras)
                {
                    source->Extras.emplace_back(std::wstring{ extraName }, std::wstring{ value });
                }
                source->FirstPage.reserve(answer->size());
                for (auto const& item : *answer)
                {
                    source->FirstPage.push_back({ std::wstring{ item.Type }, std::wstring{ item.Id } });
                }
                paging = std::move(source);
            }
        }

        auto shelf = winrt::make<winrt::HaloDesktop::implementation::Shelf>(
            winrt::hstring{ labels.Title },
            winrt::hstring{ labels.Source },
            winrt::single_threaded_vector(std::move(items)).GetView(),
            nullptr,
            winrt::HaloDesktop::implementation::ShelfSeeAll::Catalog,
            std::move(paging));
        auto* const details = winrt::get_self<winrt::HaloDesktop::implementation::Shelf>(shelf);
        details->SetKey(query.Key);
        details->SetLoading(answer == nullptr);
        return shelf;
    }

    void CatalogService::ComposeCatalogShelves()
    {
        std::vector<winrt::HaloDesktop::Shelf> shelves;
        for (auto const& slot : m_shelfBoard.Slots())
        {
            // Empty and failed shelves are not drawn; the others keep their place.
            if (slot.State != HomeShelfState::Loading && !HomeShelfShowsItems(slot.State))
            {
                continue;
            }
            auto const query = std::find_if(m_shelfQueries.begin(), m_shelfQueries.end(), [&slot](ShelfQuery const& candidate)
            {
                return candidate.Key == slot.Key;
            });
            if (query == m_shelfQueries.end())
            {
                continue;
            }
            auto built = m_builtShelves.find(slot.Key);
            if (built == m_builtShelves.end())
            {
                auto const answer = m_shelfAnswers.find(slot.Key);
                auto const* const items = HomeShelfShowsItems(slot.State) && answer != m_shelfAnswers.end()
                    ? &answer->second
                    : nullptr;
                built = m_builtShelves.emplace(slot.Key, BuildCatalogShelf(*query, items)).first;
            }
            shelves.push_back(built->second);
        }
        m_catalogShelves = winrt::single_threaded_vector(std::move(shelves)).GetView();
        m_shelves = winrt::single_threaded_vector(BuildShelves(m_catalogShelves, m_libraryItems)).GetView();
    }

    void CatalogService::PublishUserRows()
    {
        m_libraryItems = winrt::single_threaded_vector(BuildLibraryItems()).GetView();
        auto continueSnapshot = BuildContinueSnapshot();
        m_continue = continueSnapshot.Items;
        ComposeCatalogShelves();
        BeginContinueArtworkFill();
        BeginContinueNextFill(std::move(continueSnapshot.Requests));
    }

    void CatalogService::NotifyHomeChanged()
    {
        InvokeHandlers(m_homeHandlers);
    }

    HomeCatalogStatus CatalogService::CatalogStatus() const
    {
        return {
            m_shelfBoard.ListKnown(),
            !m_shelfBoard.Slots().empty(),
            m_shelfBoard.EveryShelfFailed(),
        };
    }

    CatalogChangedToken CatalogService::AddHomeChangedHandler(CatalogChangedHandler handler)
    {
        if (!handler)
        {
            return 0;
        }
        auto const token = m_nextHomeToken++;
        m_homeHandlers.emplace(token, std::move(handler));
        return token;
    }

    void CatalogService::RemoveHomeChangedHandler(CatalogChangedToken token) noexcept
    {
        m_homeHandlers.erase(token);
    }

    winrt::HaloDesktop::MediaSummary CatalogService::Hero() const
    {
        return FeaturedForFilter(0);
    }

    // Expressed through the set so both shapes obey one selection rule.
    winrt::HaloDesktop::MediaSummary CatalogService::FeaturedForFilter(
        std::int32_t filterIndex) const
    {
        auto const featured = FeaturedForHome(filterIndex, 1).Items;
        return featured && featured.Size() > 0 ? featured.GetAt(0) : nullptr;
    }

    FeaturedSelection CatalogService::FeaturedForHome(std::int32_t filterIndex, std::uint32_t count) const
    {
        FeaturedSelection selection;
        selection.Items = winrt::single_threaded_vector<winrt::HaloDesktop::MediaSummary>().GetView();
        if (!m_shelfBoard.ListKnown())
        {
            return selection;
        }

        // The answered shelves' titles end to end, in shelf order, up to the
        // first shelf still waiting; the rule reads no further than that.
        std::vector<FeaturedPoolSlot> slots;
        std::vector<winrt::HaloDesktop::MediaSummary> pool;
        for (auto const& slot : m_shelfBoard.Slots())
        {
            FeaturedPoolSlot entry;
            entry.Pending = HomeShelfPending(slot);
            if (!entry.Pending && HomeShelfShowsItems(slot.State))
            {
                if (auto const built = m_builtShelves.find(slot.Key); built != m_builtShelves.end())
                {
                    for (auto const& item : built->second.Items())
                    {
                        entry.Candidates.push_back({
                            std::wstring{ item.Type() } + L':' + std::wstring{ item.Id() },
                            item.Kind() == winrt::HaloDesktop::MediaKind::Series
                                ? ::HaloDesktop::ViewModels::HomeMediaKind::Series
                                : ::HaloDesktop::ViewModels::HomeMediaKind::Movie,
                            !item.Background().empty(),
                        });
                        pool.push_back(item);
                    }
                }
            }
            auto const pending = entry.Pending;
            slots.push_back(std::move(entry));
            if (pending)
            {
                break;
            }
        }

        auto const result = SelectSettledFeatured(
            ::HaloDesktop::ViewModels::HomeFilterFromIndex(filterIndex),
            slots,
            count);
        selection.Settled = result.Settled;
        std::vector<winrt::HaloDesktop::MediaSummary> featured;
        featured.reserve(result.Picks.size());
        for (auto const index : result.Picks)
        {
            featured.push_back(pool[index]);
        }
        selection.Items = winrt::single_threaded_vector(std::move(featured)).GetView();
        return selection;
    }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> CatalogService::ContinueWatching() const { return m_continue; }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> CatalogService::Shelves() const { return m_shelves; }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> CatalogService::LibraryItems() const { return m_libraryItems; }

    bool CatalogService::HasLoaded() const { return m_refreshState.HasLoaded(); }

    std::uint64_t CatalogService::SnapshotVersion() const noexcept { return m_snapshotVersion; }

    void CatalogService::InvalidateCatalogs() noexcept { m_refreshState.Invalidate(); }

    bool CatalogService::CatalogsDirty() const noexcept
    {
        if (m_refreshState.IsDirty())
        {
            return true;
        }
        try
        {
            return m_refreshState.HasLoaded() && CurrentCatalogLanguage() != m_appliedLanguage;
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<std::wstring> CatalogService::CurrentCatalogLanguage() const
    {
        auto const value = m_settings->CatalogLanguage();
        return value ? std::optional<std::wstring>{ std::wstring{ *value } } : std::nullopt;
    }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary>
    CatalogService::RecentlyWatchedForFilter(std::int32_t filterIndex) const
    {
        std::vector<RecentlyWatchedRow> rows;
        for (auto const& row : m_watchState->Rows())
        {
            // A tombstone is a removal from the history, not a play.
            if (row.RemovedAt)
            {
                continue;
            }
            rows.push_back({
                std::wstring{ row.ItemId },
                row.Name ? std::optional<std::wstring>{ std::wstring{ *row.Name } } : std::nullopt,
                row.Poster ? std::optional<std::wstring>{ std::wstring{ *row.Poster } } : std::nullopt,
                row.UpdatedAt,
            });
        }
        std::vector<RecentlyWatchedLibraryEntry> library;
        for (auto const& row : m_library->Rows())
        {
            library.push_back({
                std::wstring{ row.Id },
                std::wstring{ row.Name },
                row.Poster ? std::optional<std::wstring>{ std::wstring{ *row.Poster } } : std::nullopt,
                row.RemovedAt.has_value(),
            });
        }
        std::unordered_set<std::wstring> exclude;
        if (m_continue)
        {
            for (auto const& item : m_continue)
            {
                exclude.insert(std::wstring{ item.ItemId() });
            }
        }

        std::vector<winrt::HaloDesktop::MediaSummary> items;
        for (auto const& title : BuildRecentlyWatchedRow(
                 std::move(rows),
                 library,
                 exclude,
                 ::HaloDesktop::ViewModels::HomeFilterFromIndex(filterIndex)))
        {
            auto const type = winrt::hstring{ title.Type };
            items.push_back(winrt::make<winrt::HaloDesktop::implementation::MediaSummary>(
                winrt::hstring{ title.MetaId }, winrt::hstring{ title.Name }, L"", Kind(type), type,
                winrt::hstring{ title.Poster.value_or(std::wstring{}) }));
        }
        return winrt::single_threaded_vector(std::move(items)).GetView();
    }

    concurrency::task<std::vector<winrt::HaloDesktop::MediaSummary>> CatalogService::FetchCatalogPageAsync(
        std::shared_ptr<CatalogPagingSource const> source,
        std::size_t skip)
    {
        if (!source)
        {
            throw std::invalid_argument{ "A catalog page needs the catalog it pages." };
        }
        auto const uiContext = winrt::apartment_context{};
        auto const accountVersion = m_accountVersion;
        std::vector<std::pair<winrt::hstring, winrt::hstring>> extras;
        extras.reserve(source->Extras.size() + 1);
        for (auto const& [name, value] : source->Extras)
        {
            extras.emplace_back(winrt::hstring{ name }, winrt::hstring{ value });
        }
        extras.emplace_back(L"skip", winrt::to_hstring(static_cast<std::uint64_t>(skip)));
        auto const page = co_await m_backends->Current()->GetCatalogAsync(
            winrt::hstring{ source->AddonId },
            winrt::hstring{ source->Type },
            winrt::hstring{ source->CatalogId },
            std::move(extras));
        co_await uiContext;
        if (accountVersion != m_accountVersion)
        {
            throw winrt::hresult_canceled{};
        }
        std::vector<winrt::HaloDesktop::MediaSummary> items;
        items.reserve(page.size());
        for (auto const& item : page)
        {
            items.push_back(MakeMedia(item));
        }
        co_return items;
    }

    concurrency::task<std::optional<SimilarShelf>> CatalogService::FetchSimilarAsync(
        winrt::hstring type,
        winrt::hstring metaId,
        std::vector<winrt::hstring> genres)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const accountVersion = m_accountVersion;
        if (genres.empty())
        {
            co_return std::nullopt;
        }
        // A title page reached before Home has read the addons reads them here.
        auto addons = m_addons->Records();
        if (addons.empty())
        {
            co_await m_addons->LoadAsync();
            co_await uiContext;
            if (accountVersion != m_accountVersion)
            {
                throw winrt::hresult_canceled{};
            }
            addons = m_addons->Records();
        }
        // The effective order Home reads, with hidden catalogs left out.
        std::erase_if(addons, [](::HaloDesktop::Api::Dto::AddonRecord const& addon)
        {
            return addon.HideCatalogs || (!addon.IsGlobal && addon.ProvidedGlobally);
        });
        auto const source = PickGenreCatalog(addons, std::wstring_view{ type }, genres);
        if (!source)
        {
            co_return std::nullopt;
        }
        auto page = co_await m_backends->Current()->GetCatalogAsync(
            winrt::hstring{ source->AddonId },
            type,
            winrt::hstring{ source->CatalogId },
            { { winrt::hstring{ L"genre" }, winrt::hstring{ source->Genre } } });
        co_await uiContext;
        if (accountVersion != m_accountVersion)
        {
            throw winrt::hresult_canceled{};
        }
        auto const titles = SimilarTitles(std::move(page), std::wstring_view{ type }, std::wstring_view{ metaId });
        if (titles.empty())
        {
            co_return std::nullopt;
        }
        SimilarShelf shelf{ winrt::hstring{ source->Genre }, {} };
        shelf.Items.reserve(titles.size());
        for (auto const& title : titles)
        {
            shelf.Items.push_back(MakeMedia(title));
        }
        co_return shelf;
    }

    void CatalogService::RefreshContinue() { BuildContinue(); }

    void CatalogService::RebuildLibrary()
    {
        PublishUserRows();
        ++m_snapshotVersion;
    }

    std::vector<winrt::HaloDesktop::MediaSummary> CatalogService::BuildLibraryItems() const
    {
        auto libraryRows = m_library->Rows();
        auto const watch = m_watchState->Rows();
        std::unordered_map<std::wstring, std::int64_t> lastWatched;
        for (auto const& row : watch)
        {
            auto& timestamp = lastWatched[std::wstring{ row.ItemId }];
            timestamp = (std::max)(timestamp, row.UpdatedAt);
        }
        std::sort(libraryRows.begin(), libraryRows.end(), [](auto const& a, auto const& b) { return a.AddedAt > b.AddedAt; });
        std::vector<winrt::HaloDesktop::MediaSummary> libraryItems;
        libraryItems.reserve(libraryRows.size());
        for (auto const& row : libraryRows)
        {
            if (row.RemovedAt) continue;
            auto const metaId = winrt::hstring{ MetaIdFromItemId(std::wstring_view{ row.Id }) };
            libraryItems.push_back(winrt::make<winrt::HaloDesktop::implementation::MediaSummary>(
                metaId, row.Name, L"", Kind(row.Type), row.Type, row.Poster.value_or(L""), L"", L"", L"", L"", row.AddedAt,
                lastWatched[std::wstring{ row.Id }]));
        }
        return libraryItems;
    }

    std::vector<winrt::HaloDesktop::Shelf> CatalogService::BuildShelves(
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> const& catalogShelves,
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> const& libraryItems) const
    {
        std::vector<winrt::HaloDesktop::Shelf> shelves;
        shelves.reserve(catalogShelves.Size() + (libraryItems.Size() > 0 ? 1u : 0u));
        // What the viewer chose to keep leads, ahead of what the addons are
        // advertising. Home renders in-progress titles above this list, so the
        // page reads as continue watching, then the library, then the catalogs.
        if (libraryItems.Size() > 0)
        {
            shelves.push_back(winrt::make<winrt::HaloDesktop::implementation::Shelf>(
                L"My library",
                L"SYNCED · " + winrt::to_hstring(libraryItems.Size()),
                libraryItems,
                nullptr,
                winrt::HaloDesktop::implementation::ShelfSeeAll::Library,
                nullptr));
        }
        for (auto const& shelf : catalogShelves)
        {
            shelves.push_back(shelf);
        }
        return shelves;
    }

    void CatalogService::BuildContinue()
    {
        ApplyContinueSnapshot(true);
    }

    void CatalogService::ApplyContinueSnapshot(bool startFill)
    {
        auto snapshot = BuildContinueSnapshot();
        m_continue = snapshot.Items;
        BeginContinueArtworkFill();
        if (startFill)
        {
            BeginContinueNextFill(std::move(snapshot.Requests));
        }
    }

    void CatalogService::BeginContinueArtworkFill()
    {
        if (!m_continue || m_continue.Size() == 0)
        {
            return;
        }
        std::vector<winrt::HaloDesktop::ContinueItem> items;
        items.reserve(m_continue.Size());
        for (auto const& item : m_continue)
        {
            items.push_back(item);
        }
        auto const generation = m_artwork->BeginGeneration();
        // Not awaited: the snapshot above is already committed, and every card that
        // earns a still updates itself. The stamp retires this fill as soon as a
        // newer snapshot starts one, and the continuation exists only so a failed
        // lookup cannot surface as an unobserved task exception.
        m_artwork->FillAsync(std::move(items), generation).then([](concurrency::task<void> completed)
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

    CatalogService::ContinueSnapshot CatalogService::BuildContinueSnapshot() const
    {
        std::unordered_map<std::wstring, ::HaloDesktop::Api::Dto::LibraryRow> liveLibrary;
        for (auto const& row : m_library->Rows())
        {
            if (row.RemovedAt) continue;
            liveLibrary.emplace(std::wstring{ row.Id }, row);
        }

        // The library fills the gaps before the rule runs, so the shelf policy has
        // only to decide what to show rather than also where the name came from.
        std::vector<ContinueWatchRow> rows;
        auto const watch = m_watchState->Rows();
        rows.reserve(watch.size());
        for (auto const& row : watch)
        {
            auto const library = liveLibrary.find(std::wstring{ row.ItemId });
            auto const name = row.Name.value_or(
                library != liveLibrary.end() ? library->second.Name : winrt::hstring{});
            auto const poster = row.Poster.value_or(
                library != liveLibrary.end() ? library->second.Poster.value_or(L"") : winrt::hstring{});
            rows.push_back({
                std::wstring{ row.ItemId },
                std::wstring{ row.VideoId },
                std::wstring{ name },
                std::wstring{ poster },
                row.PositionSec,
                row.DurationSec,
                row.Watched,
                row.UpdatedAt,
            });
        }

        auto shelf = BuildContinueShelf(
            std::move(rows),
            [this](std::wstring const& finishedVideoId) { return m_nextEpisodes->Lookup(finishedVideoId); },
            MaximumContinueCards,
            MaximumContinueRequests);

        std::vector<winrt::HaloDesktop::ContinueItem> items;
        items.reserve(shelf.Cards.size());
        for (auto const& card : shelf.Cards)
        {
            auto const itemId = winrt::hstring{ card.ItemId };
            auto const videoId = winrt::hstring{ card.VideoId };
            auto const metaId = winrt::hstring{ MetaIdFromItemId(std::wstring_view{ itemId }) };
            auto const tag = winrt::hstring{ VideoIdTag(std::wstring_view{ videoId }, std::wstring_view{ metaId }) };
            // A promoted episode has never been started, so there is no position to
            // count down from and nothing to fill the card's bar with.
            auto const promoted = card.Kind == ContinueCardKind::NextEpisode;
            auto const timeLeft = promoted
                ? winrt::hstring{ L"UP NEXT" }
                : TimeLeft(card.DurationSec, card.PositionSec);
            auto const progress = promoted || card.DurationSec <= 0.0
                ? 0.0
                : card.PositionSec / card.DurationSec;
            items.push_back(winrt::make<winrt::HaloDesktop::implementation::ContinueItem>(
                winrt::hstring{ card.Name }, tag, tag, timeLeft, progress,
                winrt::hstring{ TypeFromItemId(std::wstring_view{ itemId }) },
                metaId, videoId, itemId, winrt::hstring{ card.Poster }));
        }
        return { winrt::single_threaded_vector(std::move(items)).GetView(), std::move(shelf.Requests) };
    }

    void CatalogService::BeginContinueNextFill(std::vector<ContinueNextRequest> requests)
    {
        if (requests.empty())
        {
            return;
        }
        auto const generation = m_nextEpisodes->BeginGeneration();
        auto const weak = weak_from_this();
        // Not awaited: the shelf above is already committed, and the promoted cards
        // join it when the lookups land. The stamp retires this fill as soon as a
        // newer snapshot starts one, and the continuation exists only so a failed
        // lookup cannot surface as an unobserved task exception.
        m_nextEpisodes->ResolveAsync(
            std::move(requests),
            generation,
            [weak]()
            {
                auto const self = weak.lock();
                if (!self)
                {
                    return;
                }
                // Already on the dispatcher thread: the service only calls back
                // there. No new fill is started, so the series whose lookups
                // failed wait for the next visit instead of being retried at once.
                self->ApplyContinueSnapshot(false);
                ++self->m_snapshotVersion;
                self->NotifyContinueChanged();
            }).then([](concurrency::task<void> completed)
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

    void CatalogService::NotifyContinueChanged()
    {
        InvokeHandlers(m_continueHandlers);
    }

    void CatalogService::InvokeHandlers(
        std::unordered_map<CatalogChangedToken, CatalogChangedHandler> const& registered)
    {
        // Copied first: a handler is free to unsubscribe from inside itself, which
        // would otherwise invalidate the iterator running it.
        std::vector<CatalogChangedHandler> handlers;
        handlers.reserve(registered.size());
        for (auto const& entry : registered)
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

    void CatalogService::PublishUserStateChange()
    {
        RebuildLibrary();
        InvokeHandlers(m_userStateHandlers);
    }

    CatalogChangedToken CatalogService::AddUserStateChangedHandler(CatalogChangedHandler handler)
    {
        if (!handler)
        {
            return 0;
        }
        auto const token = m_nextUserStateToken++;
        m_userStateHandlers.emplace(token, std::move(handler));
        return token;
    }

    void CatalogService::RemoveUserStateChangedHandler(CatalogChangedToken token) noexcept
    {
        m_userStateHandlers.erase(token);
    }

    CatalogChangedToken CatalogService::AddContinueChangedHandler(CatalogChangedHandler handler)
    {
        if (!handler)
        {
            return 0;
        }
        auto const token = m_nextContinueToken++;
        m_continueHandlers.emplace(token, std::move(handler));
        return token;
    }

    void CatalogService::RemoveContinueChangedHandler(CatalogChangedToken token) noexcept
    {
        m_continueHandlers.erase(token);
    }

    void CatalogService::OnAccountChanged()
    {
        ++m_accountVersion;
        m_loadTask.reset();
        m_refreshState.Reset();
        m_appliedLanguage.reset();
        // Retires every shelf request still out for the old account: none of
        // their tickets can match a slot again.
        m_shelfBoard.Reset();
        m_shelfQueries.clear();
        m_shelfAnswers.clear();
        m_builtShelves.clear();
        m_continue = winrt::single_threaded_vector<winrt::HaloDesktop::ContinueItem>().GetView();
        m_catalogShelves = winrt::single_threaded_vector<winrt::HaloDesktop::Shelf>().GetView();
        m_shelves = winrt::single_threaded_vector<winrt::HaloDesktop::Shelf>().GetView();
        m_libraryItems = winrt::single_threaded_vector<winrt::HaloDesktop::MediaSummary>().GetView();
        ++m_snapshotVersion;
        m_artwork->OnAccountChanged();
        m_nextEpisodes->OnAccountChanged();
    }
}
