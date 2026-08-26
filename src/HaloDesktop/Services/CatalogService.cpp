#include "pch.h"
#include "Services/CatalogService.h"

#include "Api/ApiClient.h"
#include "Models/Models.h"
#include "Services/AddonService.h"
#include "Services/LibraryService.h"
#include "Services/WatchStateService.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Storage.h>

namespace
{
    constexpr wchar_t SearchHistoryKey[] = L"halo.searchHistory.v1";

    struct CatalogQuery final
    {
        winrt::hstring AddonId;
        winrt::hstring AddonName;
        ::HaloDesktop::Api::Dto::AddonRecord::Catalog Catalog;
    };

    winrt::HaloDesktop::MediaKind Kind(winrt::hstring const& type)
    {
        return type == L"series" ? winrt::HaloDesktop::MediaKind::Series : winrt::HaloDesktop::MediaKind::Movie;
    }

    winrt::hstring MetaLine(::HaloDesktop::Api::Dto::MetaPreview const& item)
    {
        std::wstring result;
        if (item.ReleaseInfo) result.append(*item.ReleaseInfo);
        if (item.Rating)
        {
            if (!result.empty()) result.append(L" · ");
            result.append(L"★ ").append(*item.Rating);
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
            item.Rating.value_or(L""), item.ReleaseInfo.value_or(L""), addedAt, updatedAt);
    }

    winrt::hstring TypeLabel(winrt::hstring const& type)
    {
        if (type == L"movie") return L"Movies";
        if (type == L"series") return L"Series";
        return type;
    }

    winrt::hstring MetaIdFromItemId(winrt::hstring const& itemId)
    {
        std::wstring value{ itemId };
        auto const separator = value.find(L':');
        return separator == std::wstring::npos ? itemId : winrt::hstring{ value.substr(separator + 1) };
    }

    winrt::hstring TypeFromItemId(winrt::hstring const& itemId)
    {
        std::wstring value{ itemId };
        auto const separator = value.find(L':');
        return separator == std::wstring::npos ? L"movie" : winrt::hstring{ value.substr(0, separator) };
    }

    winrt::hstring EpisodeTag(winrt::hstring const& videoId, winrt::hstring const& metaId)
    {
        std::wstring video{ videoId };
        std::wstring prefix{ metaId };
        prefix.push_back(L':');
        if (!video.starts_with(prefix)) return L"MOVIE";
        auto const tail = video.substr(prefix.size());
        auto const separator = tail.find(L':');
        if (separator == std::wstring::npos) return L"SERIES";
        try
        {
            auto const season = std::stoi(tail.substr(0, separator));
            auto const episode = std::stoi(tail.substr(separator + 1));
            std::wostringstream output;
            output << L'S' << std::setw(2) << std::setfill(L'0') << season
                << L'E' << std::setw(2) << std::setfill(L'0') << episode;
            return winrt::hstring{ output.str() };
        }
        catch (...) { return L"SERIES"; }
    }

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

    std::wstring Lower(winrt::hstring const& input)
    {
        std::wstring result{ input };
        std::transform(result.begin(), result.end(), result.begin(), [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
        return result;
    }
}

namespace HaloDesktop::Services
{
    CatalogService::CatalogService(
        std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
        std::shared_ptr<AddonService> addons,
        std::shared_ptr<LibraryService> library,
        std::shared_ptr<WatchStateService> watchState)
        : m_apiClient(std::move(apiClient)), m_addons(std::move(addons)), m_library(std::move(library)), m_watchState(std::move(watchState))
    {
        if (!m_apiClient || !m_addons || !m_library || !m_watchState) throw std::invalid_argument{ "CatalogService requires all dependencies." };
        m_continue = winrt::single_threaded_vector<winrt::HaloDesktop::ContinueItem>().GetView();
        m_catalogShelves = winrt::single_threaded_vector<winrt::HaloDesktop::Shelf>().GetView();
        m_shelves = winrt::single_threaded_vector<winrt::HaloDesktop::Shelf>().GetView();
        m_libraryItems = winrt::single_threaded_vector<winrt::HaloDesktop::MediaSummary>().GetView();
        m_searchResults = winrt::single_threaded_vector<winrt::HaloDesktop::SearchGroup>().GetView();
        LoadRecentTerms();
    }

    concurrency::task<void> CatalogService::LoadAsync()
    {
        // Shell and Home both load at startup. Sharing the in-flight task keeps
        // either caller from observing a superseded but apparently successful load.
        if (m_loadTask)
        {
            auto task = *m_loadTask;
            co_await task;
            co_return;
        }

        auto task = LoadCoreAsync();
        m_loadTask = task;
        try
        {
            co_await task;
        }
        catch (...)
        {
            m_loadTask.reset();
            throw;
        }
        m_loadTask.reset();
    }

    concurrency::task<void> CatalogService::LoadCoreAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        co_await m_addons->LoadAsync();
        co_await m_library->LoadAsync();
        co_await m_watchState->LoadAsync();
        co_await uiContext;

        std::vector<CatalogQuery> queries;
        for (auto const& addon : m_addons->Records())
        {
            for (auto const& catalog : addon.Catalogs)
            {
                if (!catalog.HasRequiredExtra && queries.size() < 8) queries.push_back({ addon.Id, addon.Name, catalog });
            }
        }
        std::vector<concurrency::task<std::vector<::HaloDesktop::Api::Dto::MetaPreview>>> tasks;
        for (auto const& query : queries)
        {
            tasks.push_back(m_apiClient->GetCatalogAsync(query.AddonId, query.Catalog.Type, query.Catalog.Id));
        }
        std::vector<std::vector<::HaloDesktop::Api::Dto::MetaPreview>> results;
        for (auto& task : tasks)
        {
            try { results.push_back(co_await task); }
            catch (...) { results.emplace_back(); }
        }
        co_await uiContext;

        std::vector<winrt::HaloDesktop::Shelf> shelves;
        for (std::size_t index = 0; index < queries.size(); ++index)
        {
            if (results[index].empty()) continue;
            std::vector<winrt::HaloDesktop::MediaSummary> items;
            for (auto const& item : results[index]) items.push_back(MakeMedia(item));
            auto const title = queries[index].Catalog.Name.value_or(queries[index].AddonName);
            auto const label = TypeLabel(queries[index].Catalog.Type);
            shelves.push_back(winrt::make<winrt::HaloDesktop::implementation::Shelf>(title, label, winrt::single_threaded_vector(std::move(items)).GetView()));
        }
        m_catalogShelves = winrt::single_threaded_vector(std::move(shelves)).GetView();
        m_hero = nullptr;
        if (m_catalogShelves.Size() > 0 && m_catalogShelves.GetAt(0).Items().Size() > 0) m_hero = m_catalogShelves.GetAt(0).Items().GetAt(0);
        BuildLibraryShelf();
        BuildContinue();
        // A snapshot with nothing in it is not worth keeping. Every catalog request
        // failing looks exactly like an account with no addons, and caching that
        // would leave Home empty for the rest of the session with no way back. A
        // later reload that comes back empty therefore leaves both of these alone,
        // and whoever is already showing the last good snapshot keeps it.
        if (m_shelves.Size() > 0 || m_libraryItems.Size() > 0)
        {
            m_loaded = true;
            ++m_snapshotVersion;
        }
    }

    concurrency::task<void> CatalogService::SearchAsync(winrt::hstring query)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_searchVersion;
        std::wstring term{ query };
        term.erase(term.begin(), std::find_if(term.begin(), term.end(), [](wchar_t value) { return std::iswspace(value) == 0; }));
        term.erase(std::find_if(term.rbegin(), term.rend(), [](wchar_t value) { return std::iswspace(value) == 0; }).base(), term.end());
        if (term.size() < 2)
        {
            m_searchResults = winrt::single_threaded_vector<winrt::HaloDesktop::SearchGroup>().GetView();
            co_return;
        }

        co_await m_addons->LoadAsync();
        co_await uiContext;
        if (version != m_searchVersion)
        {
            co_return;
        }

        std::vector<CatalogQuery> queries;
        for (auto const& addon : m_addons->Records()) for (auto const& catalog : addon.Catalogs)
            if (catalog.SupportsSearch) queries.push_back({ addon.Id, addon.Name, catalog });
        std::vector<concurrency::task<std::vector<::HaloDesktop::Api::Dto::MetaPreview>>> tasks;
        for (auto const& item : queries) tasks.push_back(m_apiClient->GetCatalogAsync(item.AddonId, item.Catalog.Type, item.Catalog.Id, { { L"search", winrt::hstring{ term } } }));
        std::vector<std::vector<::HaloDesktop::Api::Dto::MetaPreview>> results;
        for (auto& task : tasks) { try { results.push_back(co_await task); } catch (...) { results.emplace_back(); } }
        co_await uiContext;
        if (version != m_searchVersion) co_return;
        std::vector<winrt::HaloDesktop::SearchGroup> groups;
        for (std::size_t index = 0; index < queries.size(); ++index)
        {
            if (results[index].empty()) continue;
            std::vector<winrt::HaloDesktop::MediaSummary> items;
            for (auto const& item : results[index]) items.push_back(MakeMedia(item));
            auto const title = queries[index].Catalog.Name.value_or(queries[index].AddonName);
            groups.push_back(winrt::make<winrt::HaloDesktop::implementation::SearchGroup>(title, queries[index].AddonName, winrt::single_threaded_vector(std::move(items)).GetView()));
        }
        m_searchResults = winrt::single_threaded_vector(std::move(groups)).GetView();
    }

    winrt::HaloDesktop::MediaSummary CatalogService::Hero() const { return m_hero; }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::ContinueItem> CatalogService::ContinueWatching() const { return m_continue; }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Shelf> CatalogService::Shelves() const { return m_shelves; }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary> CatalogService::LibraryItems() const { return m_libraryItems; }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SearchGroup> CatalogService::SearchResults() const { return m_searchResults; }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> CatalogService::RecentTerms() const { return m_recentTerms; }

    bool CatalogService::HasLoaded() const { return m_loaded; }

    std::uint64_t CatalogService::SnapshotVersion() const noexcept { return m_snapshotVersion; }

    void CatalogService::RefreshContinue() { BuildContinue(); }

    void CatalogService::BuildLibraryShelf()
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
        for (auto const& row : libraryRows)
        {
            if (row.RemovedAt) continue;
            auto const metaId = MetaIdFromItemId(row.Id);
            libraryItems.push_back(winrt::make<winrt::HaloDesktop::implementation::MediaSummary>(
                metaId, row.Name, L"", Kind(row.Type), row.Type, row.Poster.value_or(L""), L"", L"", L"", L"", row.AddedAt,
                lastWatched[std::wstring{ row.Id }]));
        }
        m_libraryItems = winrt::single_threaded_vector(std::move(libraryItems)).GetView();

        // Always rebuilt from the catalog shelves rather than appended to whatever
        // m_shelves already held, so calling this twice cannot leave two copies of
        // the library shelf on Home.
        std::vector<winrt::HaloDesktop::Shelf> shelves;
        for (auto const& shelf : m_catalogShelves) shelves.push_back(shelf);
        if (m_libraryItems.Size() > 0)
        {
            shelves.push_back(winrt::make<winrt::HaloDesktop::implementation::Shelf>(
                L"My library", L"SYNCED · " + winrt::to_hstring(m_libraryItems.Size()), m_libraryItems));
        }
        m_shelves = winrt::single_threaded_vector(std::move(shelves)).GetView();
    }

    void CatalogService::BuildContinue()
    {
        // Derives its own view of the library rather than sharing one with the shelf
        // build, which is what lets it run on its own when the user comes back to
        // Home without anything else being reloaded.
        std::unordered_map<std::wstring, ::HaloDesktop::Api::Dto::LibraryRow> liveLibrary;
        for (auto const& row : m_library->Rows())
        {
            if (row.RemovedAt) continue;
            liveLibrary.emplace(std::wstring{ row.Id }, row);
        }

        auto watch = m_watchState->Rows();
        std::sort(watch.begin(), watch.end(), [](auto const& a, auto const& b) { return a.UpdatedAt > b.UpdatedAt; });
        std::set<std::wstring> seen;
        std::vector<winrt::HaloDesktop::ContinueItem> continued;
        for (auto const& row : watch)
        {
            if (continued.size() == 8 || row.DurationSec <= 0 || row.Watched) continue;
            auto const fraction = row.PositionSec / row.DurationSec;
            if (fraction <= 0.02 || fraction >= 0.95 || !seen.insert(std::wstring{ row.ItemId }).second) continue;
            auto const library = liveLibrary.find(std::wstring{ row.ItemId });
            auto const name = row.Name.value_or(library != liveLibrary.end() ? library->second.Name : winrt::hstring{});
            if (name.empty()) continue;
            auto const poster = row.Poster.value_or(library != liveLibrary.end() ? library->second.Poster.value_or(L"") : winrt::hstring{});
            auto const type = TypeFromItemId(row.ItemId);
            auto const metaId = MetaIdFromItemId(row.ItemId);
            continued.push_back(winrt::make<winrt::HaloDesktop::implementation::ContinueItem>(
                name, EpisodeTag(row.VideoId, metaId), EpisodeTag(row.VideoId, metaId), TimeLeft(row.DurationSec, row.PositionSec), fraction,
                type, metaId, row.VideoId, row.ItemId, poster));
        }
        m_continue = winrt::single_threaded_vector(std::move(continued)).GetView();
    }

    void CatalogService::LoadRecentTerms()
    {
        std::vector<winrt::hstring> terms;
        std::set<std::wstring> seen;
        try
        {
            auto const values = winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values();
            if (values.HasKey(SearchHistoryKey))
            {
                auto const raw = winrt::unbox_value<winrt::hstring>(values.Lookup(SearchHistoryKey));
                for (auto const& item : winrt::Windows::Data::Json::JsonArray::Parse(raw))
                {
                    if (item.ValueType() != winrt::Windows::Data::Json::JsonValueType::String
                        || terms.size() == 20)
                    {
                        continue;
                    }
                    auto term = item.GetString();
                    std::wstring trimmed{ term };
                    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](wchar_t value)
                    {
                        return std::iswspace(value) == 0;
                    }));
                    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](wchar_t value)
                    {
                        return std::iswspace(value) == 0;
                    }).base(), trimmed.end());
                    if (trimmed.empty())
                    {
                        continue;
                    }
                    term = winrt::hstring{ trimmed };
                    if (seen.insert(Lower(term)).second)
                    {
                        terms.push_back(term);
                    }
                }
            }
        }
        catch (...) {}
        m_recentTerms = winrt::single_threaded_vector(std::move(terms)).GetView();
    }

    void CatalogService::RecordRecent(winrt::hstring term)
    {
        if (term.empty()) return;
        std::vector<winrt::hstring> terms{ term };
        auto const lowerTerm = Lower(term);
        std::set<std::wstring> seen{ lowerTerm };
        for (auto const& existing : m_recentTerms)
        {
            if (terms.size() == 20)
            {
                break;
            }
            if (seen.insert(Lower(existing)).second)
            {
                terms.push_back(existing);
            }
        }
        m_recentTerms = winrt::single_threaded_vector(std::move(terms)).GetView();
        SaveRecentTerms();
    }

    void CatalogService::SaveRecentTerms()
    {
        winrt::Windows::Data::Json::JsonArray array;
        for (auto const& term : m_recentTerms) array.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue(term));
        winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values().Insert(SearchHistoryKey, winrt::box_value(array.Stringify()));
    }
}
