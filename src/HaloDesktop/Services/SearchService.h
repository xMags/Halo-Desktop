#pragma once

#include "Services/HomeShelfLoadPolicy.h"
#include "Services/SearchHistoryPolicy.h"
#include "Services/SearchResultsPolicy.h"

#include "Api/Dto.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <winrt/base.h>

namespace HaloDesktop::Api { class BackendSelector; }
namespace HaloDesktop::Services
{
    class AddonService;
    class DevicePreferencesStore;
    class LibraryService;

    // How far a search has got before any catalog has been asked.
    enum class SearchPhase
    {
        // No query long enough to search.
        Idle,
        // The addons, and so the catalogs to ask, are not known yet.
        LoadingAddons,
        // The addon list could not be loaded, so no catalog could be asked.
        AddonsFailed,
        // The catalogs are asked; each answers on its own.
        Asking,
    };

    // What Search shows before a query: the first films catalog and the first
    // series catalog the addons offer, as each answered.
    struct SearchPopular final
    {
        std::vector<SearchTitle> Movies;
        std::vector<SearchTitle> Series;
        bool MoviesLoading{};
        bool SeriesLoading{};
    };

    using SearchChangedToken = std::uint64_t;
    using SearchChangedHandler = std::function<void()>;

    // Search's data: the catalogs a query is asked of, their answers as they
    // land, the popular titles, and the device's search history.
    //
    // UI-thread only, like CatalogService. Each query lays out its own board
    // of catalogs (HomeShelfBoard, reset per query), so an answer can only
    // land in the very request that asked for it: a newer query, or an account
    // change, retires every ticket still out. Requests are not cancelled on
    // the wire; their answers are discarded. Handlers are called on the UI
    // thread with no lock held, and continuations reach the service only
    // through a weak reference.
    class SearchService final : public std::enable_shared_from_this<SearchService>
    {
    public:
        SearchService(
            std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
            std::shared_ptr<AddonService> addons,
            std::shared_ptr<LibraryService> library,
            std::shared_ptr<DevicePreferencesStore> preferences);

        // Asks every searchable catalog for this query (trimmed). A query too
        // short to search, or a different one, retires what is in flight.
        // Asking for the query already asked is a no-op unless it is `force`d.
        void Search(std::wstring const& query, bool force = false);
        [[nodiscard]] SearchPhase Phase() const noexcept;
        // The query the current answers belong to, trimmed.
        [[nodiscard]] std::wstring const& Term() const noexcept;
        [[nodiscard]] std::vector<SearchTarget> const& Targets() const noexcept;
        // One state per target, in target order.
        [[nodiscard]] std::vector<SearchTargetState> States() const;

        // Starts the popular catalogs' requests when nothing usable is held.
        void EnsurePopular();
        [[nodiscard]] SearchPopular const& Popular() const noexcept;

        // The library as libraryMatches reads it, and whether a title is in it.
        [[nodiscard]] std::vector<SearchLibraryEntry> LibraryEntries() const;
        [[nodiscard]] bool InLibrary(std::wstring const& type, std::wstring const& id) const;

        // Most recent first. Recording happens only on a deliberate act: Enter,
        // or opening a result.
        [[nodiscard]] std::vector<SearchHistoryEntry> const& History() const noexcept;
        void RecordTerm(std::wstring_view term);
        void RemoveTerm(std::wstring_view term);
        void ClearHistory();

        // Advances with every account change, so a view can ask again.
        [[nodiscard]] std::uint64_t AccountVersion() const noexcept;
        [[nodiscard]] SearchChangedToken AddChangedHandler(SearchChangedHandler handler);
        void RemoveChangedHandler(SearchChangedToken token) noexcept;
        void OnAccountChanged();

    private:
        enum class PopularSlot
        {
            Movies,
            Series,
        };

        [[nodiscard]] concurrency::task<void> StartAsync(std::uint64_t serial);
        void AskTargets();
        static concurrency::task<void> RunTargetFetchAsync(
            std::weak_ptr<SearchService> weak,
            winrt::apartment_context uiContext,
            concurrency::task<std::vector<::HaloDesktop::Api::Dto::MetaPreview>> request,
            HomeShelfTicket ticket,
            std::uint64_t accountVersion);
        void ApplyTargetAnswer(
            HomeShelfTicket const& ticket,
            std::optional<std::vector<::HaloDesktop::Api::Dto::MetaPreview>> answer);
        [[nodiscard]] concurrency::task<void> LoadPopularAsync(std::uint64_t accountVersion, std::uint64_t serial);
        void SaveHistory();
        void NotifyChanged();

        std::shared_ptr<::HaloDesktop::Api::BackendSelector> m_backends;
        std::shared_ptr<AddonService> m_addons;
        std::shared_ptr<LibraryService> m_library;
        std::shared_ptr<DevicePreferencesStore> m_preferences;

        SearchPhase m_phase{ SearchPhase::Idle };
        std::wstring m_term;
        // Advances with every Search call, so a wait for the addons started
        // for one query cannot ask the catalogs for a later one.
        std::uint64_t m_serial{};
        std::uint64_t m_accountVersion{};
        HomeShelfBoard m_board;
        std::vector<SearchTarget> m_targets;
        std::unordered_map<std::wstring, std::vector<SearchTitle>> m_answers;

        SearchPopular m_popular;
        std::uint64_t m_popularSerial{};
        bool m_popularRunning{};
        std::optional<std::chrono::steady_clock::time_point> m_popularLoadedAt;

        std::vector<SearchHistoryEntry> m_history;
        std::unordered_map<SearchChangedToken, SearchChangedHandler> m_handlers;
        SearchChangedToken m_nextToken{ 1 };
    };

    // An addon's catalog entry as the search rules read it.
    [[nodiscard]] SearchTitle ToSearchTitle(::HaloDesktop::Api::Dto::MetaPreview const& preview);
}
