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
#include "Services/SearchRankingPolicy.h"
#include "Services/SearchRevealPolicy.h"
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

    // How often an unrevealed query checks whether its reveal is due.
    constexpr std::chrono::milliseconds RevealCheckInterval{ 100 };
    constexpr wchar_t AddonsUnreachableNote[] = L"Search could not reach your addons. Check your connection and try again.";

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
        m_revealTimer = m_dispatcher.CreateTimer();
        m_revealTimer.Interval(RevealCheckInterval);
        m_revealTimer.IsRepeating(true);
        m_revealTimer.Tick([weak = get_weak()](auto const&, auto const&)
        {
            if (auto const self = weak.get())
            {
                self->OnRevealTick();
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
                self->Refresh();
                return;
            }
            // Another account: what was found belongs to the old one. Asked
            // again once the change has finished.
            self->m_accountVersion = version;
            self->m_board = {};
            self->m_boardSerial.reset();
            self->m_hasShown = false;
            self->ForgetShown();
            self->Refresh();
            self->m_dispatcher.TryEnqueue([weak = self->get_weak()]()
            {
                if (auto const again = weak.get())
                {
                    again->m_search->Search(again->m_activeQuery, true);
                    again->m_search->EnsurePopular();
                    again->Refresh();
                }
            });
        });
        RebuildHistoryLists();
        Refresh();
    }

    SearchViewModel::~SearchViewModel()
    {
        if (m_debounceTimer)
        {
            m_debounceTimer.Stop();
        }
        if (m_revealTimer)
        {
            m_revealTimer.Stop();
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
        // Too short to search: back to recent and popular at once, not after the pause.
        if (!::HaloDesktop::Services::IsSearchableQuery(capped))
        {
            m_activeQuery = std::wstring{ capped };
            SearchNow();
            return;
        }
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
        Refresh();
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ClearVisibility() const noexcept { return VisibleIf(!m_query.empty()); }
    winrt::Windows::Foundation::IInspectable SearchViewModel::Suggestions() const { return m_suggestions; }
    winrt::Windows::Foundation::IInspectable SearchViewModel::RecentItems() const { return m_recent; }
    winrt::Windows::Foundation::IInspectable SearchViewModel::PopularItems() const { return m_popular; }
    winrt::Windows::Foundation::IInspectable SearchViewModel::LibraryItems() const { return m_library; }
    winrt::Windows::Foundation::IInspectable SearchViewModel::ResultItems() const { return m_results; }
    bool SearchViewModel::ShowKindBadges() const noexcept { return m_filterIndex == 0; }
    winrt::hstring SearchViewModel::ResultCountText() const { return winrt::to_hstring(static_cast<std::uint64_t>(m_resultCount)); }
    winrt::hstring SearchViewModel::StatusText() const { return m_statusText; }
    bool SearchViewModel::StillAsking() const noexcept { return m_stillAskingShown; }
    bool SearchViewModel::ResultsStale() const noexcept { return m_resultsStale; }

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
    Microsoft::UI::Xaml::Visibility SearchViewModel::NoAddonsVisibility() const noexcept { return VisibleIf(m_noAddons); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::TopMatchVisibility() const noexcept { return VisibleIf(m_topMatch != nullptr); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::TopMatchSkeletonVisibility() const noexcept { return VisibleIf(m_topMatchSkeleton); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::LibraryVisibility() const noexcept { return VisibleIf(!m_libraryShown.empty()); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::ResultsVisibility() const noexcept { return VisibleIf(m_active && m_resultCount > 0); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::ResultsSkeletonVisibility() const noexcept { return VisibleIf(m_resultsSkeleton); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::StatusVisibility() const noexcept { return VisibleIf(m_active); }
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
        if (m_board.Revealed())
        {
            ShowSubsequence();
        }
        Refresh();
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
        Refresh();
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

    void SearchViewModel::Refresh()
    {
        namespace Services = ::HaloDesktop::Services;
        auto const query = Services::TrimmedText(m_activeQuery);
        m_active = Services::IsSearchableQuery(query);
        if (!m_active)
        {
            ComposeIdle();
            return;
        }
        // The service has not been asked this query (an account change
        // cleared it, and it is asked again shortly): nothing new is known.
        if (m_search->Term() != query)
        {
            ComposePending({});
            return;
        }
        if (m_boardSerial != m_search->Serial())
        {
            StartBoard(m_search->Serial());
        }
        auto const phase = m_search->Phase();
        if (phase == Services::SearchPhase::LoadingAddons)
        {
            ComposePending({});
            return;
        }
        // With the addons not even listed, no catalog was asked: settled at
        // once, with nothing from the addons.
        auto const asking = phase == Services::SearchPhase::Asking;
        auto const states = asking ? m_search->States() : std::vector<Services::SearchTargetState>{};
        auto const progress = asking ? Services::SearchProgress(m_search->Targets(), states) : Services::SearchProgressResult{};
        if (!m_board.Revealed())
        {
            if (asking && !RevealIsDue(states, progress))
            {
                ComposePending(progress);
                return;
            }
            RevealNow(states, progress);
            return;
        }
        AbsorbLate(states);
        ComposeRevealed(progress);
        FinishCompose();
    }

    void SearchViewModel::StartBoard(std::uint64_t serial)
    {
        m_board = {};
        m_boardSerial = serial;
        m_absorbed.clear();
        m_askedAt = std::chrono::steady_clock::now();
        m_revealTimer.Start();
    }

    bool SearchViewModel::RevealIsDue(
        std::vector<::HaloDesktop::Services::SearchTargetState> const& states,
        ::HaloDesktop::Services::SearchProgressResult const& progress) const
    {
        namespace Services = ::HaloDesktop::Services;
        auto const answeredWithItems = static_cast<std::size_t>(std::count_if(states.begin(), states.end(), [](Services::SearchTargetState const& state)
        {
            return state.Status == Services::SearchTargetStatus::Done && !state.Titles.empty();
        }));
        auto const sinceAsked = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_askedAt);
        return Services::DecideReveal(progress.Pending, progress.Total, answeredWithItems, sinceAsked) == Services::RevealDecision::Reveal;
    }

    void SearchViewModel::OnRevealTick()
    {
        namespace Services = ::HaloDesktop::Services;
        if (m_board.Revealed())
        {
            m_revealTimer.Stop();
            return;
        }
        // Only the board's own query, once its catalogs are asked; anything
        // else arrives through the service's notification.
        if (!m_active
            || m_boardSerial != m_search->Serial()
            || m_search->Phase() != Services::SearchPhase::Asking
            || m_search->Term() != Services::TrimmedText(m_activeQuery))
        {
            return;
        }
        auto const states = m_search->States();
        auto const progress = Services::SearchProgress(m_search->Targets(), states);
        if (RevealIsDue(states, progress))
        {
            RevealNow(states, progress);
        }
    }

    void SearchViewModel::ComposeIdle()
    {
        namespace Services = ::HaloDesktop::Services;
        m_revealTimer.Stop();
        m_board = {};
        m_boardSerial.reset();
        m_absorbed.clear();
        m_hasShown = false;
        ForgetShown();

        // Popular, before a search: the first films and first series catalogs
        // taking turns, narrowed by the kind filter.
        auto const type = FilterType();
        auto const& popular = m_search->Popular();
        std::vector<std::vector<SearchTitle>> popularLists{
            type && *type == L"series" ? std::vector<SearchTitle>{} : popular.Movies,
            type && *type == L"movie" ? std::vector<SearchTitle>{} : popular.Series,
        };
        auto popularTitles = Services::Interleave(popularLists);
        if (popularTitles.size() > Services::SearchPopularShown) popularTitles.resize(Services::SearchPopularShown);
        auto const popularLoading = (!(type && *type == L"series") && popular.MoviesLoading)
            || (!(type && *type == L"movie") && popular.SeriesLoading);
        m_popularSection = popularLoading || !popularTitles.empty();
        m_popularSkeleton = popularTitles.empty();
        Sync(m_popular, m_popularShown, std::move(popularTitles));

        m_resultsStale = false;
        m_topMatchSkeleton = false;
        m_resultsSkeleton = false;
        m_stillAskingShown = false;
        m_statusText = {};
        m_noAddons = false;
        m_filteredEmpty = false;
        m_nothingFound = false;
        FinishCompose();
    }

    void SearchViewModel::ComposePending(::HaloDesktop::Services::SearchProgressResult const& progress)
    {
        // The previous query's results stay, dimmed; placeholders only when
        // nothing has been shown since the box was last idle.
        m_popularSection = false;
        m_resultsStale = m_hasShown;
        m_topMatchSkeleton = !m_hasShown;
        m_resultsSkeleton = !m_hasShown;
        m_stillAskingShown = true;
        m_statusText = progress.Total > 0
            ? winrt::hstring{ ::HaloDesktop::Services::SearchStillAskingNote(progress.Pending) }
            : winrt::hstring{};
        m_noAddons = false;
        m_filteredEmpty = false;
        m_nothingFound = false;
        m_resultCount = m_displayed.size();
        FinishCompose();
    }

    void SearchViewModel::RevealNow(
        std::vector<::HaloDesktop::Services::SearchTargetState> const& states,
        ::HaloDesktop::Services::SearchProgressResult const& progress)
    {
        namespace Services = ::HaloDesktop::Services;
        auto const query = Services::TrimmedText(m_activeQuery);
        auto const answers = AnswersFrom(states, false);
        m_board.Reveal(answers, Services::ParseSearchQuery(query));
        m_revealTimer.Stop();
        m_libraryAll = Services::LibraryMatches(m_search->LibraryEntries(), query);
        m_hasShown = true;
        // Chosen before the kind filter, so the filter does not move it.
        SetTopMatch(m_board.TopMatch());
        ShowSubsequence();
        ComposeRevealed(progress);
        FinishCompose();
    }

    std::vector<::HaloDesktop::Services::SearchAnswer> SearchViewModel::AnswersFrom(
        std::vector<::HaloDesktop::Services::SearchTargetState> const& states,
        bool onlyNew)
    {
        namespace Services = ::HaloDesktop::Services;
        auto const& targets = m_search->Targets();
        auto const& ranks = m_search->TargetAddonRanks();
        std::vector<Services::SearchAnswer> answers;
        auto const count = (std::min)(targets.size(), states.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            auto const& state = states[index];
            if (state.Status == Services::SearchTargetStatus::Pending)
            {
                continue;
            }
            auto const fresh = m_absorbed.insert(targets[index].Key).second;
            if ((onlyNew && !fresh) || state.Status != Services::SearchTargetStatus::Done || state.Titles.empty())
            {
                continue;
            }
            // A target the service gave no rank for goes after every ranked one.
            auto const rank = index < ranks.size() ? ranks[index] : targets.size();
            answers.push_back({ targets[index], state.Titles, rank });
        }
        return answers;
    }

    void SearchViewModel::AbsorbLate(std::vector<::HaloDesktop::Services::SearchTargetState> const& states)
    {
        // Each diff is applied before the next answer is absorbed, so its
        // appends start where the grid's board indices end.
        for (auto const& answer : AnswersFrom(states, true))
        {
            auto const diff = m_board.Absorb(answer);
            if (!diff.Empty())
            {
                ApplyDiff(diff);
            }
        }
    }

    void SearchViewModel::ApplyDiff(::HaloDesktop::Services::BoardDiff const& diff)
    {
        if (diff.TopMatchUpdate)
        {
            SetTopMatch(diff.TopMatchUpdate);
        }
        for (auto const& [index, title] : diff.Updates)
        {
            auto const at = std::lower_bound(m_displayed.begin(), m_displayed.end(), index);
            if (at == m_displayed.end() || *at != index)
            {
                // Held back by the kind chip or the library row; the board keeps it current.
                continue;
            }
            m_results.SetAt(static_cast<std::uint32_t>(at - m_displayed.begin()), ToMedia(title));
        }
        auto const firstAppend = m_board.Titles().size() - diff.Appends.size();
        for (std::size_t offset = 0; offset < diff.Appends.size(); ++offset)
        {
            auto const& title = diff.Appends[offset];
            if (!ShownInGrid(title))
            {
                continue;
            }
            m_displayed.push_back(firstAppend + offset);
            m_results.Append(ToMedia(title));
        }
        m_resultCount = m_displayed.size();
    }

    bool SearchViewModel::ShownInGrid(SearchTitle const& title) const
    {
        if (auto const type = FilterType(); type && title.Type != *type)
        {
            return false;
        }
        auto const identities = ::HaloDesktop::Services::TitleIdentities(title);
        return std::none_of(identities.begin(), identities.end(), [this](std::wstring const& identity)
        {
            return m_libraryIdentities.contains(identity);
        });
    }

    void SearchViewModel::ShowSubsequence()
    {
        auto library = OfType(m_libraryAll, FilterType());
        m_libraryIdentities.clear();
        for (auto const& title : library)
        {
            for (auto& identity : ::HaloDesktop::Services::TitleIdentities(title)) m_libraryIdentities.insert(std::move(identity));
        }
        Sync(m_library, m_libraryShown, std::move(library));

        auto const& titles = m_board.Titles();
        m_displayed.clear();
        std::vector<winrt::Windows::Foundation::IInspectable> media;
        for (std::size_t index = 0; index < titles.size(); ++index)
        {
            if (!ShownInGrid(titles[index]))
            {
                continue;
            }
            m_displayed.push_back(index);
            media.push_back(ToMedia(titles[index]));
        }
        m_results.ReplaceAll(media);
        m_resultCount = m_displayed.size();
    }

    void SearchViewModel::ComposeRevealed(::HaloDesktop::Services::SearchProgressResult const& progress)
    {
        namespace Services = ::HaloDesktop::Services;
        auto const phase = m_search->Phase();
        auto const pending = progress.Pending > 0;
        m_popularSection = false;
        m_resultsStale = false;
        m_topMatchSkeleton = false;
        m_resultsSkeleton = false;
        m_stillAskingShown = pending;
        if (phase == Services::SearchPhase::AddonsFailed)
        {
            // No catalog could be asked at all, which reads as every one failing.
            m_statusText = AddonsUnreachableNote;
        }
        else if (pending)
        {
            m_statusText = winrt::hstring{ Services::SearchStillAskingNote(progress.Pending) };
        }
        else
        {
            m_statusText = winrt::hstring{ Services::SearchFailureNote(progress.Failed, progress.Total) };
        }
        m_noAddons = phase == Services::SearchPhase::Asking && progress.Total == 0;
        m_resultCount = m_displayed.size();
        // Only once every catalog has settled, so an empty state never flashes
        // before a slow answer.
        auto const type = FilterType();
        auto const settledEmpty = !pending && m_libraryShown.empty() && m_resultCount == 0;
        m_filteredEmpty = settledEmpty && !progress.Groups.empty() && type.has_value();
        m_nothingFound = settledEmpty && !m_topMatchTitle && !type && progress.Total > 0
            && progress.Failed.size() < progress.Total;
    }

    void SearchViewModel::ForgetShown()
    {
        m_displayed.clear();
        if (m_results.Size() > 0)
        {
            m_results.Clear();
        }
        m_libraryAll.clear();
        m_libraryIdentities.clear();
        Sync(m_library, m_libraryShown, {});
        SetTopMatch(std::nullopt);
        m_resultCount = 0;
    }

    void SearchViewModel::SetTopMatch(std::optional<SearchTitle> title)
    {
        if (title == m_topMatchTitle)
        {
            return;
        }
        m_topMatch = title ? ToMedia(*title) : nullptr;
        m_topMatchTitle = std::move(title);
    }

    void SearchViewModel::FinishCompose()
    {
        m_topMatchInLibrary = m_topMatchTitle && m_search->InLibrary(m_topMatchTitle->Type, m_topMatchTitle->Id);
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
                 L"ResultCountText", L"StatusText", L"StillAsking", L"ResultsStale", L"FilteredEmptyText",
                 L"TopMatch", L"TopMatchTitle", L"TopMatchRating", L"TopMatchKind", L"TopMatchYear", L"TopMatchBlurb",
                 L"TopMatchPoster", L"TopMatchMetaId", L"TopMatchBackdrop", L"TopMatchSoftBackdrop", L"TopMatchActionLabel",
                 L"TopMatchRatingVisibility", L"TopMatchYearVisibility", L"TopMatchInLibraryVisibility", L"TopMatchBlurbVisibility",
                 L"RecentVisibility", L"PopularVisibility", L"PopularGridVisibility", L"PopularSkeletonVisibility",
                 L"StatusVisibility", L"NoAddonsVisibility", L"TopMatchVisibility", L"TopMatchSkeletonVisibility",
                 L"LibraryVisibility", L"ResultsVisibility", L"ResultsSkeletonVisibility",
                 L"FilteredEmptyVisibility", L"NothingFoundVisibility" })
        {
            Raise(name);
        }
    }
}
