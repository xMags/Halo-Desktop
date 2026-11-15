#include "pch.h"
#include "ViewModels/SearchViewModel.h"
#if __has_include("RecentSearchViewModel.g.cpp")
#include "RecentSearchViewModel.g.cpp"
#endif
#if __has_include("SearchViewModel.g.cpp")
#include "SearchViewModel.g.cpp"
#endif
#include "Controls/ArtworkUrlPolicy.h"
#include "Models/Models.h"
#include "Services/NavigationService.h"
#include "Services/PlayableVideoPolicy.h"
#include "Services/RatingPolicy.h"
#include "ViewModels/ObservableHelper.h"

#include <algorithm>
#include <chrono>
#include <span>
#include <utility>

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    using ::HaloDesktop::Services::SearchTitle;

    winrt::Microsoft::UI::Xaml::Visibility VisibleIf(bool value) noexcept
    {
        return value ? Visible : Collapsed;
    }

    bool Truthy(std::optional<std::wstring> const& value) noexcept
    {
        return value.has_value() && !value->empty();
    }

    std::optional<std::wstring_view> View(std::optional<std::wstring> const& value)
    {
        return value ? std::optional<std::wstring_view>{ *value } : std::nullopt;
    }

    // The card's line: the release line and the rating, as Home's cards read.
    winrt::hstring MetaLine(SearchTitle const& title)
    {
        std::wstring line = title.ReleaseInfo.value_or(L"");
        if (auto const rating = ::HaloDesktop::Services::RatingLabel(View(title.Rating)))
        {
            if (!line.empty()) line.append(L" · ");
            line.append(*rating);
        }
        return winrt::hstring{ line };
    }

    winrt::HaloDesktop::MediaSummary ToMedia(SearchTitle const& title)
    {
        auto const kind = title.Type == L"series"
            ? winrt::HaloDesktop::MediaKind::Series
            : winrt::HaloDesktop::MediaKind::Movie;
        return winrt::make<winrt::HaloDesktop::implementation::MediaSummary>(
            winrt::hstring{ title.Id },
            winrt::hstring{ title.Name },
            MetaLine(title),
            kind,
            winrt::hstring{ title.Type },
            winrt::hstring{ title.Poster.value_or(L"") },
            winrt::hstring{ title.Background.value_or(L"") },
            winrt::hstring{ title.Description.value_or(L"") },
            winrt::hstring{ ::HaloDesktop::Services::RatedValue(View(title.Rating)).value_or(L"") },
            winrt::hstring{ title.ReleaseInfo.value_or(L"") },
            0,
            0,
            winrt::hstring{ title.Logo.value_or(L"") },
            winrt::hstring{ title.DefaultVideoId.value_or(L"") });
    }

    // Brings a collection to `next` without resetting it when only some cards
    // changed (an addon's copy filling in a rating), and without touching it
    // at all when nothing did, so the realised cards are left alone.
    void Sync(
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> const& items,
        std::vector<SearchTitle>& shown,
        std::vector<SearchTitle> next)
    {
        if (next == shown)
        {
            return;
        }
        auto const sameKeys = next.size() == shown.size()
            && std::equal(next.begin(), next.end(), shown.begin(), [](SearchTitle const& a, SearchTitle const& b)
            {
                return a.Type == b.Type && a.Id == b.Id;
            });
        if (sameKeys)
        {
            for (std::uint32_t index = 0; index < next.size(); ++index)
            {
                if (!(next[index] == shown[index]))
                {
                    items.SetAt(index, ToMedia(next[index]));
                }
            }
        }
        else
        {
            std::vector<winrt::Windows::Foundation::IInspectable> media;
            media.reserve(next.size());
            for (auto const& title : next)
            {
                media.push_back(ToMedia(title));
            }
            items.ReplaceAll(media);
        }
        shown = std::move(next);
    }

    std::vector<SearchTitle> OfType(std::vector<SearchTitle> titles, std::optional<std::wstring> const& type)
    {
        if (type)
        {
            std::erase_if(titles, [&type](SearchTitle const& title) { return title.Type != *type; });
        }
        return titles;
    }
}

namespace winrt::HaloDesktop::implementation
{
    RecentSearchViewModel::RecentSearchViewModel(winrt::hstring term) : m_term(std::move(term)) {}
    winrt::hstring RecentSearchViewModel::Term() const { return m_term; }
    winrt::hstring RecentSearchViewModel::RemoveLabel() const { return L"Remove " + m_term + L" from recent searches"; }

    SearchViewModel::SearchViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_search(services.Search),
          m_navigation(services.Navigation),
          m_dispatcher(Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread()),
          m_suggestions(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_recent(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_popular(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_library(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_results(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        if (!m_search || !m_navigation || !m_dispatcher)
        {
            throw winrt::hresult_invalid_argument{ L"Search needs its service, navigation and a dispatcher." };
        }
        m_accountVersion = m_search->AccountVersion();
        m_debounceTimer = m_dispatcher.CreateTimer();
        m_debounceTimer.Interval(std::chrono::milliseconds{ ::HaloDesktop::Services::SearchDebounceMilliseconds });
        m_debounceTimer.IsRepeating(false);
        m_debounceTimer.Tick([weak = get_weak()](auto const&, auto const&)
        {
            if (auto const self = weak.get())
            {
                self->m_activeQuery = std::wstring{ self->m_query };
                self->SearchNow();
            }
        });
        m_searchToken = m_search->AddChangedHandler([weak = get_weak()]()
        {
            auto const self = weak.get();
            if (!self)
            {
                return;
            }
            auto const version = self->m_search->AccountVersion();
            if (version == self->m_accountVersion)
            {
                self->RebuildHistoryLists();
                self->Recompose();
                return;
            }
            // Another account: what was found and what failed to load belong
            // to the old one. Asked again once the change has finished.
            self->m_accountVersion = version;
            self->m_missingArt.clear();
            self->m_dispatcher.TryEnqueue([weak = self->get_weak()]()
            {
                if (auto const again = weak.get())
                {
                    again->m_search->Search(again->m_activeQuery, true);
                    again->m_search->EnsurePopular();
                    again->Recompose();
                }
            });
        });
        RebuildHistoryLists();
        Recompose();
    }

    SearchViewModel::~SearchViewModel()
    {
        if (m_debounceTimer)
        {
            m_debounceTimer.Stop();
        }
        if (m_search && m_searchToken != 0)
        {
            m_search->RemoveChangedHandler(m_searchToken);
        }
    }

    winrt::hstring SearchViewModel::Query() const { return m_query; }

    void SearchViewModel::Query(winrt::hstring const& value)
    {
        auto const capped = winrt::hstring{ ::HaloDesktop::Services::CappedQuery(value) };
        if (capped == m_query)
        {
            return;
        }
        SetQueryText(capped);
        m_debounceTimer.Stop();
        m_debounceTimer.Start();
    }

    void SearchViewModel::SetQueryText(winrt::hstring const& value)
    {
        if (value == m_query)
        {
            return;
        }
        m_query = value;
        Raise(L"Query");
        Raise(L"ClearVisibility");
        RebuildHistoryLists();
    }

    void SearchViewModel::SearchNow()
    {
        m_debounceTimer.Stop();
        // A search the addons could not even be listed for is asked again.
        auto const retry = m_search->Phase() == ::HaloDesktop::Services::SearchPhase::AddonsFailed;
        m_search->Search(m_activeQuery, retry);
        Recompose();
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ClearVisibility() const noexcept { return VisibleIf(!m_query.empty()); }
    winrt::Windows::Foundation::IInspectable SearchViewModel::Suggestions() const { return m_suggestions; }
    winrt::Windows::Foundation::IInspectable SearchViewModel::RecentItems() const { return m_recent; }
    winrt::Windows::Foundation::IInspectable SearchViewModel::PopularItems() const { return m_popular; }
    winrt::Windows::Foundation::IInspectable SearchViewModel::LibraryItems() const { return m_library; }
    winrt::Windows::Foundation::IInspectable SearchViewModel::ResultItems() const { return m_results; }
    bool SearchViewModel::ShowKindBadges() const noexcept { return m_filterIndex == 0; }
    winrt::hstring SearchViewModel::ResultCountText() const { return winrt::to_hstring(static_cast<std::uint64_t>(m_resultCount)); }
    winrt::hstring SearchViewModel::FailureNote() const { return m_failureNote; }
    winrt::hstring SearchViewModel::StillAskingText() const { return m_stillAsking; }
    bool SearchViewModel::StillAsking() const noexcept { return m_stillAskingShown; }

    winrt::hstring SearchViewModel::FilteredEmptyText() const
    {
        auto const label = m_filterIndex == 1 ? L"movies" : m_filterIndex == 2 ? L"series" : L"titles";
        return winrt::hstring{ std::wstring{ L"No " } + label + L" match “"
            + ::HaloDesktop::Services::TrimmedText(m_activeQuery) + L"”." };
    }

    winrt::HaloDesktop::MediaSummary SearchViewModel::TopMatch() const { return m_topMatch; }
    winrt::hstring SearchViewModel::TopMatchTitle() const { return m_topMatchTitle ? winrt::hstring{ m_topMatchTitle->Name } : winrt::hstring{}; }

    winrt::hstring SearchViewModel::TopMatchRating() const
    {
        if (!m_topMatchTitle) return {};
        return winrt::hstring{ ::HaloDesktop::Services::RatingLabel(View(m_topMatchTitle->Rating)).value_or(L"") };
    }

    winrt::hstring SearchViewModel::TopMatchKind() const
    {
        return m_topMatchTitle && m_topMatchTitle->Type == L"series" ? L"SERIES" : L"MOVIE";
    }

    winrt::hstring SearchViewModel::TopMatchYear() const
    {
        return m_topMatchTitle ? winrt::hstring{ m_topMatchTitle->ReleaseInfo.value_or(L"") } : winrt::hstring{};
    }

    winrt::hstring SearchViewModel::TopMatchBlurb() const
    {
        return m_topMatchTitle ? winrt::hstring{ m_topMatchTitle->Description.value_or(L"") } : winrt::hstring{};
    }

    // The addon's poster unless its answer already admits there is none; the
    // control falls back on metahub's by the id.
    winrt::hstring SearchViewModel::TopMatchPoster() const
    {
        if (!m_topMatchTitle || ::HaloDesktop::Services::HasNoPoster(*m_topMatchTitle)) return {};
        return winrt::hstring{ *m_topMatchTitle->Poster };
    }

    winrt::hstring SearchViewModel::TopMatchMetaId() const
    {
        return m_topMatchTitle ? winrt::hstring{ m_topMatchTitle->Id } : winrt::hstring{};
    }

    // The backdrop when the addon sent one; otherwise the poster, softened.
    winrt::hstring SearchViewModel::TopMatchBackdrop() const
    {
        if (!m_topMatchTitle || !Truthy(m_topMatchTitle->Background)) return {};
        return winrt::hstring{ ::HaloDesktop::Controls::CardArtworkUrl(*m_topMatchTitle->Background) };
    }

    winrt::hstring SearchViewModel::TopMatchSoftBackdrop() const
    {
        if (!m_topMatchTitle || Truthy(m_topMatchTitle->Background)) return {};
        auto const poster = ::HaloDesktop::Services::HasNoPoster(*m_topMatchTitle)
            ? ::HaloDesktop::Controls::MetahubPosterUrl(m_topMatchTitle->Id)
            : m_topMatchTitle->Poster;
        return poster ? winrt::hstring{ ::HaloDesktop::Controls::CardArtworkUrl(*poster) } : winrt::hstring{};
    }

    winrt::hstring SearchViewModel::TopMatchActionLabel() const
    {
        return m_topMatchTitle && m_topMatchTitle->Type == L"series" ? L"Choose episode" : L"Play";
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::TopMatchRatingVisibility() const noexcept
    {
        try { return VisibleIf(!TopMatchRating().empty()); } catch (...) { return Collapsed; }
    }
    Microsoft::UI::Xaml::Visibility SearchViewModel::TopMatchYearVisibility() const noexcept
    {
        return VisibleIf(m_topMatchTitle && Truthy(m_topMatchTitle->ReleaseInfo));
    }
    Microsoft::UI::Xaml::Visibility SearchViewModel::TopMatchInLibraryVisibility() const noexcept { return VisibleIf(m_topMatchInLibrary); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::TopMatchBlurbVisibility() const noexcept
    {
        return VisibleIf(m_topMatchTitle && Truthy(m_topMatchTitle->Description));
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::SuggestionsVisibility() const noexcept { return VisibleIf(m_suggestionsShown); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::RecentVisibility() const noexcept { return VisibleIf(m_recentShown); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::PopularVisibility() const noexcept { return VisibleIf(m_popularSection); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::PopularGridVisibility() const noexcept { return VisibleIf(m_popularSection && !m_popularSkeleton); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::PopularSkeletonVisibility() const noexcept { return VisibleIf(m_popularSection && m_popularSkeleton); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::FailureNoteVisibility() const noexcept { return VisibleIf(!m_failureNote.empty()); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::NoAddonsVisibility() const noexcept { return VisibleIf(m_noAddons); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::TopMatchVisibility() const noexcept { return VisibleIf(m_topMatch != nullptr); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::TopMatchSkeletonVisibility() const noexcept { return VisibleIf(m_topMatchSkeleton); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::LibraryVisibility() const noexcept { return VisibleIf(!m_libraryShown.empty()); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::ResultsVisibility() const noexcept { return VisibleIf(m_active && m_resultCount > 0); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::ResultsSkeletonVisibility() const noexcept { return VisibleIf(m_resultsSkeleton); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::StillAskingVisibility() const noexcept { return VisibleIf(m_stillAskingShown); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::FilteredEmptyVisibility() const noexcept { return VisibleIf(m_filteredEmpty); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::NothingFoundVisibility() const noexcept { return VisibleIf(m_nothingFound); }

    bool SearchViewModel::SkeletonsShown() const noexcept
    {
        return (m_popularSection && m_popularSkeleton) || m_topMatchSkeleton || m_resultsSkeleton;
    }

    void SearchViewModel::SetFilter(std::int32_t index)
    {
        if (index < 0 || index > 2 || index == m_filterIndex)
        {
            return;
        }
        m_filterIndex = index;
        Raise(L"ShowKindBadges");
        Recompose();
    }

    void SearchViewModel::SetBoxFocused(bool focused)
    {
        if (m_boxFocused == focused)
        {
            return;
        }
        m_boxFocused = focused;
        RebuildHistoryLists();
    }

    void SearchViewModel::Submit()
    {
        if (!::HaloDesktop::Services::IsSearchableQuery(m_query))
        {
            return;
        }
        m_activeQuery = std::wstring{ m_query };
        SearchNow();
        m_search->RecordTerm(m_query);
    }

    void SearchViewModel::ClearQuery()
    {
        Query(L"");
    }

    void SearchViewModel::SearchAgain(winrt::hstring const& term)
    {
        auto const capped = winrt::hstring{ ::HaloDesktop::Services::CappedQuery(term) };
        SetQueryText(capped);
        m_activeQuery = std::wstring{ capped };
        SearchNow();
        m_search->RecordTerm(capped);
    }

    void SearchViewModel::Forget(winrt::hstring const& term) { m_search->RemoveTerm(term); }
    void SearchViewModel::ForgetAll() { m_search->ClearHistory(); }
    void SearchViewModel::ShowAllResults() { SetFilter(0); }

    void SearchViewModel::RecordQuery()
    {
        m_search->RecordTerm(::HaloDesktop::Services::TrimmedText(m_activeQuery));
    }

    void SearchViewModel::OpenDetail(winrt::Windows::Foundation::IInspectable const& item)
    {
        auto const media = item.try_as<winrt::HaloDesktop::MediaSummary>();
        if (!media)
        {
            return;
        }
        RecordQuery();
        m_navigation->GoTo(
            ::HaloDesktop::Services::Page::Detail,
            winrt::make<winrt::HaloDesktop::implementation::DetailNavParams>(media.Type(), media.Id(), media.Title(), media.Poster()));
    }

    void SearchViewModel::OpenTopMatch()
    {
        OpenDetail(m_topMatch);
    }

    void SearchViewModel::PlayTopMatch()
    {
        if (!m_topMatch)
        {
            return;
        }
        auto const media = m_topMatch;
        if (media.Kind() == winrt::HaloDesktop::MediaKind::Series
            || !::HaloDesktop::Services::PlayableFromPreview(media.Id(), media.DefaultVideoId()))
        {
            OpenTopMatch();
            return;
        }
        RecordQuery();
        m_navigation->ShowSheet(
            ::HaloDesktop::Services::Page::Sources,
            winrt::make<winrt::HaloDesktop::implementation::SourcesNavParams>(
                media.Type(),
                media.Id(),
                winrt::hstring{ ::HaloDesktop::Services::MovieVideoId(media.Id(), media.DefaultVideoId()) },
                media.Type() + L":" + media.Id(),
                media.Title(),
                media.Title(),
                L"",
                media.Poster()));
    }

    void SearchViewModel::ReportMissingArt(winrt::Windows::Foundation::IInspectable const& item)
    {
        auto const media = item.try_as<winrt::HaloDesktop::MediaSummary>();
        if (!media || !m_missingArt.insert(std::wstring{ media.Type() } + L':' + std::wstring{ media.Id() }).second)
        {
            return;
        }
        // Several cards fail together as a screenful loads: one recompose
        // after them, not one each.
        m_dispatcher.TryEnqueue(
            Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [weak = get_weak()]()
            {
                if (auto const self = weak.get())
                {
                    self->Recompose();
                }
            });
    }

    void SearchViewModel::Arrive(winrt::hstring const& query)
    {
        auto const capped = winrt::hstring{ ::HaloDesktop::Services::CappedQuery(query) };
        SetQueryText(capped);
        m_activeQuery = std::wstring{ capped };
        SearchNow();
    }

    void SearchViewModel::Shown()
    {
        m_search->EnsurePopular();
        Recompose();
    }

    std::optional<std::wstring> SearchViewModel::FilterType() const
    {
        if (m_filterIndex == 1) return std::wstring{ L"movie" };
        if (m_filterIndex == 2) return std::wstring{ L"series" };
        return std::nullopt;
    }

    void SearchViewModel::RebuildHistoryLists()
    {
        auto const& history = m_search->History();
        auto const terms = ::HaloDesktop::Services::SearchHistoryTerms(history);

        // Each list is rebuilt only when its terms change, so typing does not
        // reset the pills or the suggestions under the pointer.
        auto const fill = [](auto const& items, std::vector<std::wstring>& shown, std::vector<std::wstring> next)
        {
            if (next == shown) return;
            std::vector<winrt::Windows::Foundation::IInspectable> rows;
            rows.reserve(next.size());
            for (auto const& term : next) rows.push_back(winrt::make<RecentSearchViewModel>(winrt::hstring{ term }));
            items.ReplaceAll(rows);
            shown = std::move(next);
        };
        std::vector<std::wstring> recent(
            terms.begin(),
            terms.begin() + static_cast<std::ptrdiff_t>((std::min)(terms.size(), ::HaloDesktop::Services::SearchRecentShown)));
        fill(m_recent, m_recentTerms, std::move(recent));
        fill(m_suggestions, m_suggestionTerms, m_boxFocused
            ? ::HaloDesktop::Services::SuggestedSearches(terms, m_query)
            : std::vector<std::wstring>{});
        auto const suggestionsShown = !m_suggestionTerms.empty();
        auto const recentShown = !m_active && !m_recentTerms.empty();
        if (suggestionsShown != m_suggestionsShown)
        {
            m_suggestionsShown = suggestionsShown;
            Raise(L"SuggestionsVisibility");
        }
        if (recentShown != m_recentShown)
        {
            m_recentShown = recentShown;
            Raise(L"RecentVisibility");
        }
    }

    void SearchViewModel::Recompose()
    {
        namespace Services = ::HaloDesktop::Services;
        auto const query = Services::TrimmedText(m_activeQuery);
        m_active = Services::IsSearchableQuery(query);
        auto const type = FilterType();
        auto const phase = m_search->Phase();
        auto const current = m_active && m_search->Term() == query;
        auto const waitingForAddons = current && phase == Services::SearchPhase::LoadingAddons;
        auto const asking = current && phase == Services::SearchPhase::Asking;

        Services::SearchProgressResult progress;
        if (asking)
        {
            auto const states = m_search->States();
            progress = Services::SearchProgress(m_search->Targets(), states, std::wstring_view{ query });
        }
        auto const pending = progress.Pending > 0 || waitingForAddons;
        auto const waiting = m_active && pending;

        // Picked before the kind filter, so the filter does not move it, and
        // shown once above everything, so the lists below leave it out; so do
        // the results for whatever the library already shows.
        std::vector<SearchTitle> fromLibrary;
        if (m_active)
        {
            auto const library = m_search->LibraryEntries();
            fromLibrary = OfType(Services::LibraryMatches(library, query), type);
        }
        std::vector<SearchTitle> shownAbove = fromLibrary;
        if (progress.TopMatch) shownAbove.push_back(*progress.TopMatch);
        std::vector<std::vector<SearchTitle>> lists;
        std::vector<std::vector<SearchTitle>> allLists;
        lists.reserve(progress.Groups.size());
        allLists.reserve(progress.Groups.size() + 1);
        if (progress.TopMatch) allLists.push_back({ *progress.TopMatch });
        for (auto const& group : progress.Groups)
        {
            lists.push_back(OfType(group.Titles, type));
            allLists.push_back(group.Titles);
        }
        auto const merged = Services::Interleave(lists, shownAbove);
        auto results = Services::RefineResults(merged, query, m_missingArt);

        // The top match as the card shows it: the same title, with what the
        // other addons' copies of it know.
        std::optional<SearchTitle> topShown;
        if (progress.TopMatch)
        {
            auto const enriched = Services::Interleave(allLists);
            topShown = enriched.empty() ? *progress.TopMatch : enriched.front();
        }
        if (topShown != m_topMatchTitle)
        {
            m_topMatchTitle = topShown;
            m_topMatch = topShown ? ToMedia(*topShown) : nullptr;
        }
        m_topMatchInLibrary = m_topMatchTitle && m_search->InLibrary(m_topMatchTitle->Type, m_topMatchTitle->Id);

        // Popular, before a search: the first films and first series catalogs
        // taking turns, narrowed by the kind filter.
        auto const& popular = m_search->Popular();
        std::vector<std::vector<SearchTitle>> popularLists{
            type && *type == L"series" ? std::vector<SearchTitle>{} : popular.Movies,
            type && *type == L"movie" ? std::vector<SearchTitle>{} : popular.Series,
        };
        auto popularTitles = Services::Interleave(popularLists);
        if (popularTitles.size() > Services::SearchPopularShown) popularTitles.resize(Services::SearchPopularShown);
        auto const popularLoading = (!(type && *type == L"series") && popular.MoviesLoading)
            || (!(type && *type == L"movie") && popular.SeriesLoading);
        m_popularSection = !m_active && (popularLoading || !popularTitles.empty());
        m_popularSkeleton = popularTitles.empty();

        Sync(m_popular, m_popularShown, std::move(popularTitles));
        Sync(m_library, m_libraryShown, std::move(fromLibrary));
        m_resultCount = results.size();
        Sync(m_results, m_resultsShown, m_active ? std::move(results) : std::vector<SearchTitle>{});

        if (current && phase == Services::SearchPhase::AddonsFailed)
        {
            // No catalog could be asked at all, which reads as every one failing.
            m_failureNote = L"Search could not reach your addons. Check your connection and try again.";
        }
        else
        {
            m_failureNote = m_active ? winrt::hstring{ Services::SearchFailureNote(progress.Failed, progress.Total) } : winrt::hstring{};
        }
        m_noAddons = asking && progress.Total == 0;
        m_topMatchSkeleton = waiting && !progress.TopMatch;
        m_resultsSkeleton = waiting && m_resultCount == 0;
        m_stillAskingShown = waiting && m_resultCount > 0;
        m_stillAsking = winrt::hstring{ Services::SearchStillAskingNote(progress.Pending) };
        auto const settledEmpty = m_active && !pending && m_libraryShown.empty() && m_resultCount == 0;
        m_filteredEmpty = settledEmpty && !progress.Groups.empty() && type.has_value();
        m_nothingFound = settledEmpty && !progress.TopMatch && !type && progress.Total > 0
            && progress.Failed.size() < progress.Total;
        m_recentShown = !m_active && m_recent.Size() > 0;
        RaiseAll();
    }

    winrt::event_token SearchViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void SearchViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void SearchViewModel::Raise(wchar_t const* name)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, name);
    }

    void SearchViewModel::RaiseAll()
    {
        for (auto const name : {
                 L"ResultCountText", L"FailureNote", L"StillAskingText", L"StillAsking", L"FilteredEmptyText",
                 L"TopMatch", L"TopMatchTitle", L"TopMatchRating", L"TopMatchKind", L"TopMatchYear", L"TopMatchBlurb",
                 L"TopMatchPoster", L"TopMatchMetaId", L"TopMatchBackdrop", L"TopMatchSoftBackdrop", L"TopMatchActionLabel",
                 L"TopMatchRatingVisibility", L"TopMatchYearVisibility", L"TopMatchInLibraryVisibility", L"TopMatchBlurbVisibility",
                 L"RecentVisibility", L"PopularVisibility", L"PopularGridVisibility", L"PopularSkeletonVisibility",
                 L"FailureNoteVisibility", L"NoAddonsVisibility", L"TopMatchVisibility", L"TopMatchSkeletonVisibility",
                 L"LibraryVisibility", L"ResultsVisibility", L"ResultsSkeletonVisibility", L"StillAskingVisibility",
                 L"FilteredEmptyVisibility", L"NothingFoundVisibility" })
        {
            Raise(name);
        }
    }
}
