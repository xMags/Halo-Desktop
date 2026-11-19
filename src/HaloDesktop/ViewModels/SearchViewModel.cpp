#include "pch.h"
#include "ViewModels/SearchViewModel.h"
#if __has_include("RecentSearchViewModel.g.cpp")
#include "RecentSearchViewModel.g.cpp"
#endif
#if __has_include("SearchPersonViewModel.g.cpp")
#include "SearchPersonViewModel.g.cpp"
#endif
#if __has_include("SearchViewModel.g.cpp")
#include "SearchViewModel.g.cpp"
#endif
#include "Controls/ArtworkUrlPolicy.h"
#include "Models/Models.h"
#include "Services/NavigationService.h"
#include "Services/PeopleRowPolicy.h"
#include "Services/PosterCheckService.h"
#include "Services/PeopleService.h"
#include "Services/RatingPolicy.h"
#include "Services/SearchRankingPolicy.h"
#include "Services/SearchRevealPolicy.h"
#include "Services/ServerFeaturesService.h"
#include "Services/TitleLookupService.h"
#include "ViewModels/DetailFactsPolicy.h"
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
    // The status line while a query's results are not revealed yet.
    constexpr wchar_t SearchingNote[] = L"Searching…";
    // The named person's films, kept apart from every catalog's key.
    constexpr wchar_t CreditsTargetKey[] = L"people/credits";

    // The action card's words; a magnet's come from MagnetCardPolicy.
    constexpr wchar_t LookingUpPrefix[] = L"Looking up ";
    constexpr wchar_t LookingUpSecondary[] = L"Asking your addons which title this is.";
    constexpr wchar_t NoAddonKnowsTitleText[] = L"No addon knows this title";
    constexpr wchar_t NotFoundSuffix[] = L" was not found by any of your addons.";
    constexpr wchar_t CannotLookUpText[] = L"This link can't be looked up";
    constexpr wchar_t TvdbSlugText[] = L"This TVDB link has no number in it. Paste the IMDb link instead.";
    // TVDB numbers and IMDb people, which the server looks up.
    constexpr wchar_t NeedsServerText[] = L"This kind of link needs a Halo server that supports it.";
    constexpr wchar_t AskingServerTitleText[] = L"Asking the server which title this is.";
    constexpr wchar_t AskingServerPersonText[] = L"Asking the server who this is.";
    constexpr wchar_t ServerUnknownTitleText[] = L"The server doesn't know this title";
    constexpr wchar_t ServerUnknownPersonText[] = L"The server doesn't know this person";
    constexpr wchar_t ServerNotFoundSuffix[] = L" was not found.";
    constexpr wchar_t ServerNoAnswerText[] = L"The server did not answer. Try again in a moment.";
    constexpr wchar_t OpenPersonText[] = L"Open person page";
    constexpr wchar_t WebLinkSecondary[] = L"Plays straight from this address. Not added to your history.";
    constexpr wchar_t CannotPlayText[] = L"This link can't be played";
    constexpr wchar_t OpenTitleText[] = L"Open title";
    constexpr wchar_t PlayLinkText[] = L"Play this link";
    // The legend's link item, with magnets and without.
    constexpr wchar_t LinksHintWithMagnets[] = L"HTTP & Magnet Links";
    constexpr wchar_t LinksHintPlain[] = L"HTTP Links";
    // The box's placeholder, with people search and without.
    constexpr wchar_t PlaceholderWithPeople[] = L"Search films, series and people, or paste a link";
    constexpr wchar_t PlaceholderTitlesOnly[] = L"Search films and series, or paste a link";

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
    SearchPersonViewModel::SearchPersonViewModel(::HaloDesktop::Api::Dto::PersonSummary const& person)
        : m_tmdbId(person.TmdbId),
          m_name(person.Name),
          m_line(::HaloDesktop::Services::PersonRowLine(person)),
          m_photo(person.Photo.value_or(winrt::hstring{})),
          m_initials(::HaloDesktop::ViewModels::Initials(person.Name))
    {
    }

    Microsoft::UI::Xaml::Visibility SearchPersonViewModel::LineVisibility() const noexcept { return VisibleIf(!m_line.empty()); }

    RecentSearchViewModel::RecentSearchViewModel(winrt::hstring term) : m_term(std::move(term)) {}
    winrt::hstring RecentSearchViewModel::Term() const { return m_term; }
    winrt::hstring RecentSearchViewModel::RemoveLabel() const { return L"Remove " + m_term + L" from recent searches"; }

    SearchViewModel::SearchViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_search(services.Search),
          m_navigation(services.Navigation),
          m_titleLookup(services.TitleLookup),
          m_features(services.ServerFeatures),
          m_peopleService(services.People),
          m_magnets(services.Magnets),
          m_posterChecks(services.PosterChecks),
          m_dispatcher(Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread()),
          m_suggestions(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_recent(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_library(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_results(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_people(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        if (!m_search || !m_navigation || !m_titleLookup || !m_features || !m_peopleService || !m_magnets || !m_posterChecks || !m_dispatcher)
        {
            throw winrt::hresult_invalid_argument{ L"Search needs its services, navigation and a dispatcher." };
        }
        // Asked on arrival (Shown); this only hears the answer.
        m_featuresToken = m_features->AddChangedHandler([weak = get_weak()]()
        {
            if (auto const self = weak.get())
            {
                self->OnFeaturesChanged();
            }
        });
        m_accountVersion = m_search->AccountVersion();
        m_debounceTimer = m_dispatcher.CreateTimer();
        m_debounceTimer.Interval(std::chrono::milliseconds{ ::HaloDesktop::Services::SearchDebounceMilliseconds });
        m_debounceTimer.IsRepeating(false);
        m_debounceTimer.Tick([weak = get_weak()](auto const&, auto const&)
        {
            if (auto const self = weak.get())
            {
                self->m_activeQuery = ::HaloDesktop::Services::CappedQuery(self->m_query);
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
            // Who matched belongs to the old account's server too.
            self->m_peopleQuery.clear();
            self->m_peopleAsked = false;
            self->m_peopleAnswer.reset();
            self->ForgetCredits();
            self->m_idLookup.reset();
            self->Refresh();
            self->m_dispatcher.TryEnqueue([weak = self->get_weak()]()
            {
                if (auto const again = weak.get())
                {
                    // The IMDb card's lookup was asked of the old account's
                    // backend, and the change retired it; asked again here,
                    // after every service has heard of the change.
                    if (again->m_input.Kind == ::HaloDesktop::Services::SearchInputKind::ImdbTitle)
                    {
                        again->StartTitleLookup();
                        again->RaiseAction();
                    }
                    // So is the server's lookup, once the new account's
                    // server says what it offers.
                    if (again->IsServerLookup())
                    {
                        again->StartServerLookup();
                        again->RaiseAction();
                    }
                    // And the magnet's request, which the change forgot.
                    if (again->IsMagnet())
                    {
                        again->StartMagnet();
                        again->RaiseAction();
                    }
                    again->m_search->Search(again->m_activeQuery, true);
                    again->AskPeople(false);
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
        if (m_titleLookup && m_lookupState == LookupState::Looking)
        {
            m_titleLookup->Cancel();
        }
        if (m_features && m_featuresToken != 0)
        {
            m_features->RemoveChangedHandler(m_featuresToken);
        }
        if (m_peopleService)
        {
            m_peopleService->CancelSearch();
            m_peopleService->CancelLookup();
        }
        if (m_magnets && m_magnetTracked)
        {
            m_magnets->Stop();
            m_magnets->SetWatching(false);
        }
    }

    winrt::hstring SearchViewModel::Query() const { return m_query; }

    void SearchViewModel::Query(winrt::hstring const& value)
    {
        // The box holds the whole text, so a long pasted link is read whole;
        // only words are cut to the search's length (CappedQuery) below.
        if (value == m_query)
        {
            return;
        }
        SetQueryText(value);
        m_debounceTimer.Stop();
        if (ApplyInput())
        {
            return;
        }
        // Too short to search: back to the empty page at once, not after the pause.
        if (!::HaloDesktop::Services::IsSearchableQuery(value))
        {
            m_activeQuery = ::HaloDesktop::Services::CappedQuery(value);
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
        AskPeople(false);
        Refresh();
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ClearVisibility() const noexcept { return VisibleIf(!m_query.empty()); }
    winrt::Windows::Foundation::IInspectable SearchViewModel::Suggestions() const { return m_suggestions; }
    winrt::Windows::Foundation::IInspectable SearchViewModel::RecentItems() const { return m_recent; }
    winrt::Windows::Foundation::IInspectable SearchViewModel::LibraryItems() const { return m_library; }
    winrt::Windows::Foundation::IInspectable SearchViewModel::ResultItems() const { return m_results; }
    winrt::Windows::Foundation::IInspectable SearchViewModel::PeopleItems() const { return m_people; }

    // People are not films or series, so a kind chip leaves them out.
    Microsoft::UI::Xaml::Visibility SearchViewModel::PeopleAboveVisibility() const noexcept
    {
        return VisibleIf(m_peoplePlace == PeoplePlace::Above && m_filterIndex == 0);
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::PeopleBelowVisibility() const noexcept
    {
        return VisibleIf(m_peoplePlace == PeoplePlace::Below && m_filterIndex == 0);
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::PeopleHintVisibility() const noexcept { return VisibleIf(PeopleAvailable()); }

    winrt::hstring SearchViewModel::LinksHintText() const
    {
        return m_features->Magnets() ? LinksHintWithMagnets : LinksHintPlain;
    }

    winrt::hstring SearchViewModel::Placeholder() const
    {
        return PeopleAvailable() ? PlaceholderWithPeople : PlaceholderTitlesOnly;
    }

    bool SearchViewModel::PeopleAvailable() const noexcept
    {
        return m_features->People();
    }
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
    Microsoft::UI::Xaml::Visibility SearchViewModel::HintsVisibility() const noexcept { return VisibleIf(!m_active && !ActionShown()); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::NoAddonsVisibility() const noexcept { return VisibleIf(m_noAddons); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::TopMatchVisibility() const noexcept { return VisibleIf(m_topMatch != nullptr); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::TopMatchSkeletonVisibility() const noexcept { return VisibleIf(m_topMatchSkeleton); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::LibraryVisibility() const noexcept { return VisibleIf(!m_libraryShown.empty()); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::ResultsVisibility() const noexcept { return VisibleIf(m_active && m_resultCount > 0); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::ResultsSkeletonVisibility() const noexcept { return VisibleIf(m_resultsSkeleton); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::StatusVisibility() const noexcept { return VisibleIf(m_active); }
    bool SearchViewModel::StatusFailed() const noexcept { return m_statusFailed; }
    Microsoft::UI::Xaml::Visibility SearchViewModel::FilteredEmptyVisibility() const noexcept { return VisibleIf(m_filteredEmpty); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::NothingFoundVisibility() const noexcept { return VisibleIf(m_nothingFound); }
    Microsoft::UI::Xaml::Visibility SearchViewModel::AddonsUnreachableVisibility() const noexcept { return VisibleIf(m_addonsUnreachable); }

    bool SearchViewModel::SkeletonsShown() const noexcept
    {
        return m_topMatchSkeleton || m_resultsSkeleton;
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
        if (ActionShown())
        {
            ActivateAction();
            return;
        }
        if (!::HaloDesktop::Services::IsSearchableQuery(m_query))
        {
            return;
        }
        auto capped = ::HaloDesktop::Services::CappedQuery(m_query);
        // The top match of exactly what the box holds: Enter opens it, as the
        // key hint beside its button says. Text not searched yet, or results
        // still awaited, is searched instead.
        if (m_topMatch && !m_resultsStale && capped == m_activeQuery)
        {
            OpenTopMatch();
            return;
        }
        m_activeQuery = std::move(capped);
        SearchNow();
        RecordIfText(m_activeQuery);
    }

    void SearchViewModel::ClearQuery()
    {
        Query(L"");
    }

    void SearchViewModel::SearchAgain(winrt::hstring const& term)
    {
        auto const capped = winrt::hstring{ ::HaloDesktop::Services::CappedQuery(term) };
        SetQueryText(capped);
        m_debounceTimer.Stop();
        if (ApplyInput())
        {
            return;
        }
        m_activeQuery = std::wstring{ capped };
        SearchNow();
        RecordIfText(capped);
    }

    void SearchViewModel::RetrySearch()
    {
        if (!m_active || ActionShown())
        {
            return;
        }
        // Forced: the same query, every catalog asked again, and the people
        // when they could not be had.
        m_search->Search(m_activeQuery, true);
        AskPeople(true);
        Refresh();
    }

    void SearchViewModel::OpenSettings()
    {
        m_navigation->GoTo(::HaloDesktop::Services::Page::Settings);
    }

    void SearchViewModel::Forget(winrt::hstring const& term) { m_search->RemoveTerm(term); }
    void SearchViewModel::ForgetAll() { m_search->ClearHistory(); }
    void SearchViewModel::ShowAllResults() { SetFilter(0); }

    void SearchViewModel::RecordQuery()
    {
        RecordIfText(m_activeQuery);
    }

    void SearchViewModel::RecordIfText(std::wstring_view term)
    {
        namespace Services = ::HaloDesktop::Services;
        auto const trimmed = Services::TrimmedText(term);
        if (ActionShown() || Services::ClassifySearchInput(trimmed).Kind != Services::SearchInputKind::Text)
        {
            return;
        }
        m_search->RecordTerm(trimmed);
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

    void SearchViewModel::OpenPerson(winrt::Windows::Foundation::IInspectable const& item)
    {
        auto const person = item.try_as<winrt::HaloDesktop::SearchPersonViewModel>();
        if (!person || !PeopleAvailable())
        {
            return;
        }
        auto const self = winrt::get_self<SearchPersonViewModel>(person);
        if (self->TmdbId() <= 0)
        {
            return;
        }
        RecordQuery();
        m_navigation->GoTo(
            ::HaloDesktop::Services::Page::Person,
            winrt::make<winrt::HaloDesktop::implementation::PersonNavParams>(self->TmdbId(), self->Name(), self->Photo()));
    }

    void SearchViewModel::Arrive(winrt::hstring const& query)
    {
        auto const capped = winrt::hstring{ ::HaloDesktop::Services::CappedQuery(query) };
        SetQueryText(capped);
        m_debounceTimer.Stop();
        if (ApplyInput())
        {
            return;
        }
        m_activeQuery = std::wstring{ capped };
        SearchNow();
    }

    void SearchViewModel::Shown()
    {
        // People search, server lookups and magnets follow what the server offers.
        m_features->EnsureLoaded();
        m_onScreen = true;
        UpdateMagnetWatching();
        Refresh();
    }

    void SearchViewModel::Hidden()
    {
        m_onScreen = false;
        UpdateMagnetWatching();
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
            ? ::HaloDesktop::Services::SuggestedSearches(terms, ::HaloDesktop::Services::CappedQuery(m_query))
            : std::vector<std::wstring>{});
        auto const suggestionsShown = !m_suggestionTerms.empty();
        auto const recentShown = !m_active && !ActionShown() && !m_recentTerms.empty();
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
        if (ActionShown())
        {
            ComposeAction();
            return;
        }
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
        ::HaloDesktop::Services::SearchProgressResult const& progress)
    {
        namespace Services = ::HaloDesktop::Services;
        auto const answeredWithItems = static_cast<std::size_t>(std::count_if(states.begin(), states.end(), [](Services::SearchTargetState const& state)
        {
            return state.Status == Services::SearchTargetStatus::Done && !state.Titles.empty();
        }));
        auto const sinceAsked = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_askedAt);
        // Asked about as answers come in, so the links are known by the reveal.
        auto const postersSettled = PostersSettled(states, sinceAsked);
        // Whether the words are a person's name decides which titles stay, so
        // the people answer is waited for like a catalog, up to the deadline.
        if (m_peopleAsked && !m_peopleAnswer && sinceAsked < Services::SearchRevealDeadline
            && m_peopleQuery == Services::TrimmedText(m_activeQuery))
        {
            return false;
        }
        // So are the films of the person they name, a little longer: ranked
        // among the results when in by then, after the last card otherwise.
        if (m_creditsAsked && !m_credits && sinceAsked < Services::SearchCreditsDeadline
            && m_creditsQuery == Services::TrimmedText(m_activeQuery))
        {
            return false;
        }
        return Services::DecideReveal(progress.Pending, progress.Total, answeredWithItems, sinceAsked) == Services::RevealDecision::Reveal
            && postersSettled;
    }

    ::HaloDesktop::Services::SearchQueryMeaning SearchViewModel::QueryMeaning(std::wstring const& query) const
    {
        namespace Services = ::HaloDesktop::Services;
        // An answer still out reads as not a person.
        auto const namesPerson = m_peopleAnswer && m_peopleQuery == query
            && Services::ExactNamePerson(*m_peopleAnswer, query).has_value();
        return namesPerson ? Services::SearchQueryMeaning::Person : Services::SearchQueryMeaning::Title;
    }

    bool SearchViewModel::PostersSettled(
        std::vector<::HaloDesktop::Services::SearchTargetState> const& states,
        std::chrono::milliseconds sinceAsked)
    {
        namespace Services = ::HaloDesktop::Services;
        auto const query = Services::TrimmedText(m_activeQuery);
        auto const answers = AnswersFrom(states, false);
        auto const links = Services::RevealPosterLinks(
            answers, Services::ParseSearchQuery(query), QueryMeaning(query), Services::SearchPosterCheckLimit);
        m_posterChecks->Check(links);
        if (sinceAsked >= Services::SearchPosterCheckDeadline)
        {
            return true;
        }
        return std::none_of(links.begin(), links.end(), [this](std::wstring const& link)
        {
            return m_posterChecks->StateOf(link) == Services::PosterLinkState::Unknown;
        });
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
        if (m_peopleAsked)
        {
            m_peopleService->CancelSearch();
        }
        m_peopleQuery.clear();
        m_peopleAsked = false;
        m_peopleAnswer.reset();
        ForgetCredits();
        m_board = {};
        m_boardSerial.reset();
        m_absorbed.clear();
        m_hasShown = false;
        ForgetShown();

        m_resultsStale = false;
        m_topMatchSkeleton = false;
        m_resultsSkeleton = false;
        m_stillAskingShown = false;
        m_statusFailed = false;
        m_statusText = {};
        m_noAddons = false;
        m_addonsUnreachable = false;
        m_filteredEmpty = false;
        m_nothingFound = false;
        FinishCompose();
    }

    void SearchViewModel::ComposePending([[maybe_unused]] ::HaloDesktop::Services::SearchProgressResult const& progress)
    {
        // The previous query's results stay, dimmed; placeholders only when
        // nothing has been shown since the box was last idle. Until the
        // reveal the line says only that the search is under way; how many
        // catalogs are still out is said once results are on screen.
        m_resultsStale = m_hasShown;
        m_topMatchSkeleton = !m_hasShown;
        m_resultsSkeleton = !m_hasShown;
        m_stillAskingShown = true;
        m_statusFailed = false;
        m_statusText = SearchingNote;
        m_noAddons = false;
        m_addonsUnreachable = false;
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
        auto answers = AnswersFrom(states, false);
        // A poster link its host has no image for: the title is ranked as one
        // without artwork, for good. A link not answered yet is taken as fine.
        for (auto& answer : answers)
        {
            for (auto& title : answer.Titles)
            {
                title.PosterMissing = title.Poster
                    && m_posterChecks->StateOf(*title.Poster) == Services::PosterLinkState::Missing;
            }
        }
        // A person's exact name keeps the films and series the addons found
        // for them; an answer still out reads as not a person, for good.
        m_board.Reveal(answers, Services::ParseSearchQuery(query), QueryMeaning(query));
        m_revealTimer.Stop();
        m_libraryAll = Services::LibraryMatches(m_search->LibraryEntries(), query);
        m_hasShown = true;
        // Chosen before the kind filter, so the filter does not move it.
        SetTopMatch(m_board.TopMatch());
        ShowSubsequence();
        // The people row belongs to this reveal: in place now when its answer
        // is already in, else below the results whenever it lands.
        ForgetPeople();
        m_revealedQuery = query;
        auto titles = m_board.Titles();
        if (m_board.TopMatch()) titles.push_back(*m_board.TopMatch());
        m_bestTitleTier = Services::BestTitleTier(titles, query);
        if (m_peopleAnswer && m_peopleQuery == query)
        {
            PlacePeople(PeoplePlace::Above);
        }
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
        // The films of the person the query names, after every addon's
        // answer: a copy of a title an addon also found fills that card.
        if (m_credits && !m_credits->empty() && m_creditsQuery == Services::TrimmedText(m_activeQuery))
        {
            auto const fresh = m_absorbed.insert(CreditsTargetKey).second;
            if (!onlyNew || fresh)
            {
                Services::SearchTarget target;
                target.Key = CreditsTargetKey;
                answers.push_back({ std::move(target), *m_credits, targets.size() });
            }
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
        m_resultsStale = false;
        m_topMatchSkeleton = false;
        m_resultsSkeleton = false;
        m_stillAskingShown = pending;
        m_noAddons = phase == Services::SearchPhase::Asking && progress.Total == 0;
        m_resultCount = m_displayed.size();
        // Only once every catalog has settled, so an empty state never flashes
        // before a slow answer.
        auto const type = FilterType();
        auto const settledEmpty = !pending && m_libraryShown.empty() && m_resultCount == 0;
        m_filteredEmpty = settledEmpty && !progress.Groups.empty() && type.has_value();
        m_nothingFound = settledEmpty && !m_topMatchTitle && !type && progress.Total > 0
            && progress.Failed.size() < progress.Total && m_peoplePlace == PeoplePlace::None;
        // No catalog could be asked at all (the addons could not be listed),
        // or every one asked failed and nothing is on screen: the body says
        // so, and the status line stays empty so it is not said twice.
        auto const everyOneFailed = !pending && progress.Total > 0 && progress.Failed.size() >= progress.Total;
        m_addonsUnreachable = phase == Services::SearchPhase::AddonsFailed
            || (everyOneFailed && settledEmpty && !m_topMatchTitle);
        if (m_addonsUnreachable)
        {
            m_statusText = {};
            m_statusFailed = false;
        }
        else if (pending)
        {
            m_statusText = winrt::hstring{ Services::SearchStillAskingNote(progress.Pending) };
            m_statusFailed = false;
        }
        else
        {
            m_statusText = winrt::hstring{ Services::SearchFailureNote(progress.Failed, progress.Total) };
            m_statusFailed = !m_statusText.empty();
        }
    }

    void SearchViewModel::ForgetShown()
    {
        ForgetPeople();
        m_revealedQuery.clear();
        m_bestTitleTier.reset();
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

    bool SearchViewModel::ActionShown() const noexcept
    {
        return m_input.Kind != ::HaloDesktop::Services::SearchInputKind::Text;
    }

    bool SearchViewModel::ApplyInput()
    {
        namespace Services = ::HaloDesktop::Services;
        auto input = Services::ClassifySearchInput(Services::TrimmedText(m_query));
        if (input.Kind == Services::SearchInputKind::Text)
        {
            if (!ActionShown())
            {
                return false;
            }
            // Back to words: the card goes, and the words are searched as usual.
            m_titleLookup->Cancel();
            m_peopleService->CancelLookup();
            StopMagnet();
            m_input = {};
            m_lookupState = LookupState::None;
            m_lookupTitle.reset();
            m_idLookup.reset();
            m_serverLookupAsked = false;
            Refresh();
            return false;
        }
        if (input == m_input)
        {
            return true;
        }
        m_input = std::move(input);
        m_lookupState = LookupState::None;
        m_lookupTitle.reset();
        m_idLookup.reset();
        m_serverLookupAsked = false;
        if (m_input.Kind == Services::SearchInputKind::ImdbTitle)
        {
            StartTitleLookup();
        }
        else
        {
            m_titleLookup->Cancel();
        }
        if (IsServerLookup())
        {
            StartServerLookup();
        }
        else
        {
            m_peopleService->CancelLookup();
        }
        if (IsMagnet())
        {
            StartMagnet();
        }
        else
        {
            StopMagnet();
        }
        // No catalog is asked about a link; what an earlier query asked is dropped.
        m_activeQuery.clear();
        m_search->Search(m_activeQuery, false);
        Refresh();
        return true;
    }

    void SearchViewModel::StartTitleLookup()
    {
        m_lookupState = LookupState::Looking;
        m_lookupTitle.reset();
        m_titleLookup->Lookup(m_input.ImdbId, [weak = get_weak(), id = m_input.ImdbId](std::optional<SearchTitle> title)
        {
            auto const self = weak.get();
            if (!self
                || self->m_input.Kind != ::HaloDesktop::Services::SearchInputKind::ImdbTitle
                || self->m_input.ImdbId != id)
            {
                return;
            }
            self->m_lookupState = title ? LookupState::Found : LookupState::NotFound;
            self->m_lookupTitle = std::move(title);
            self->RaiseAction();
        });
    }

    bool SearchViewModel::IsServerLookup() const noexcept
    {
        using Kind = ::HaloDesktop::Services::SearchInputKind;
        return m_input.Kind == Kind::TvdbNumeric || m_input.Kind == Kind::ImdbPerson;
    }

    void SearchViewModel::StartServerLookup()
    {
        namespace Services = ::HaloDesktop::Services;
        m_idLookup.reset();
        m_serverLookupAsked = false;
        m_features->EnsureLoaded();
        // Until the server says what it offers, the card reads as looking up;
        // its answer (OnFeaturesChanged) asks or says it cannot.
        if (!m_features->Settled())
        {
            m_lookupState = LookupState::Looking;
            return;
        }
        if (!m_features->Lookup())
        {
            m_peopleService->CancelLookup();
            m_lookupState = LookupState::Unsupported;
            return;
        }
        m_lookupState = LookupState::Looking;
        m_serverLookupAsked = true;
        auto const kind = m_input.Kind;
        auto const id = kind == Services::SearchInputKind::TvdbNumeric ? m_input.TvdbId : m_input.PersonId;
        std::wstring source = kind == Services::SearchInputKind::TvdbNumeric ? L"tvdb" : L"imdb";
        m_peopleService->Lookup(std::move(source), id, [weak = get_weak(), kind, id](
            Services::PeopleOutcome outcome,
            std::optional<::HaloDesktop::Api::Dto::IdLookup> found)
        {
            auto const self = weak.get();
            if (!self || self->m_input.Kind != kind
                || (kind == Services::SearchInputKind::TvdbNumeric ? self->m_input.TvdbId : self->m_input.PersonId) != id)
            {
                return;
            }
            // A TVDB number names a title and an IMDb person id a person;
            // any other answer is no answer for this card.
            auto const expected = kind == Services::SearchInputKind::TvdbNumeric
                ? ::HaloDesktop::Api::Dto::IdLookupKind::Title
                : ::HaloDesktop::Api::Dto::IdLookupKind::Person;
            if (outcome == Services::PeopleOutcome::Found && found && found->Kind == expected)
            {
                self->m_lookupState = LookupState::Found;
                self->m_idLookup = std::move(found);
            }
            else
            {
                self->m_lookupState = outcome == Services::PeopleOutcome::Failed ? LookupState::Failed : LookupState::NotFound;
                self->m_idLookup.reset();
            }
            self->RaiseAction();
        });
    }

    bool SearchViewModel::ServerTitleFound() const noexcept
    {
        return m_input.Kind == ::HaloDesktop::Services::SearchInputKind::TvdbNumeric
            && m_lookupState == LookupState::Found && m_idLookup
            && m_idLookup->Kind == ::HaloDesktop::Api::Dto::IdLookupKind::Title;
    }

    bool SearchViewModel::ServerPersonFound() const noexcept
    {
        return m_input.Kind == ::HaloDesktop::Services::SearchInputKind::ImdbPerson
            && m_lookupState == LookupState::Found && m_idLookup
            && m_idLookup->Kind == ::HaloDesktop::Api::Dto::IdLookupKind::Person;
    }

    void SearchViewModel::OpenServerLookup()
    {
        if (ServerTitleFound())
        {
            m_navigation->GoTo(
                ::HaloDesktop::Services::Page::Detail,
                winrt::make<winrt::HaloDesktop::implementation::DetailNavParams>(
                    m_idLookup->Type, m_idLookup->Id, m_idLookup->Name, m_idLookup->Poster.value_or(winrt::hstring{})));
            return;
        }
        if (ServerPersonFound() && PeopleAvailable())
        {
            m_navigation->GoTo(
                ::HaloDesktop::Services::Page::Person,
                winrt::make<winrt::HaloDesktop::implementation::PersonNavParams>(
                    m_idLookup->TmdbId, m_idLookup->Name, m_idLookup->Photo.value_or(winrt::hstring{})));
        }
    }

    void SearchViewModel::AskPeople(bool retry)
    {
        namespace Services = ::HaloDesktop::Services;
        auto query = Services::TrimmedText(m_activeQuery);
        if (ActionShown() || !Services::IsSearchableQuery(query) || !PeopleAvailable())
        {
            return;
        }
        // Asked once per query, beside its catalogs; again only when a retry
        // follows an answer that could not be had.
        if (m_peopleAsked && query == m_peopleQuery && !(retry && m_peopleFailed))
        {
            return;
        }
        m_peopleQuery = query;
        m_peopleAsked = true;
        m_peopleAnswer.reset();
        ForgetCredits();
        m_peopleFailed = false;
        m_peopleService->SearchPeople(std::move(query), [weak = get_weak(), asked = m_peopleQuery](
            Services::PeopleOutcome outcome,
            std::vector<::HaloDesktop::Api::Dto::PersonSummary> people)
        {
            if (auto const self = weak.get())
            {
                self->OnPeopleAnswer(asked, outcome, std::move(people));
            }
        });
    }

    void SearchViewModel::OnPeopleAnswer(
        std::wstring const& query,
        ::HaloDesktop::Services::PeopleOutcome outcome,
        std::vector<::HaloDesktop::Api::Dto::PersonSummary> people)
    {
        if (!m_peopleAsked || query != m_peopleQuery)
        {
            return;
        }
        m_peopleFailed = outcome == ::HaloDesktop::Services::PeopleOutcome::Failed;
        m_peopleAnswer = outcome == ::HaloDesktop::Services::PeopleOutcome::Found
            ? std::move(people)
            : std::vector<::HaloDesktop::Api::Dto::PersonSummary>{};
        if (auto const named = ::HaloDesktop::Services::ExactNamePerson(*m_peopleAnswer, query))
        {
            AskCredits(query, (*m_peopleAnswer)[*named].TmdbId);
        }
        // Before the reveal the answer waits for it (RevealNow). After it,
        // the row goes under the results: nothing already shown moves.
        if (m_board.Revealed() && m_revealedQuery == query && !m_peopleDecided)
        {
            PlacePeople(PeoplePlace::Below);
            Refresh();
        }
    }

    void SearchViewModel::AskCredits(std::wstring const& query, std::int64_t tmdbId)
    {
        ForgetCredits();
        m_creditsQuery = query;
        m_creditsAsked = true;
        m_peopleService->LoadFilmography(tmdbId, [weak = get_weak(), query](
            ::HaloDesktop::Services::PeopleOutcome,
            std::optional<::HaloDesktop::Api::Dto::PersonDetail> person)
        {
            if (auto const self = weak.get())
            {
                self->OnCredits(query, std::move(person));
            }
        });
    }

    void SearchViewModel::OnCredits(std::wstring const& query, std::optional<::HaloDesktop::Api::Dto::PersonDetail> person)
    {
        if (!m_creditsAsked || query != m_creditsQuery)
        {
            return;
        }
        // No answer reads as no films: the addons' results stand as they are.
        m_credits = person
            ? ::HaloDesktop::Services::CreditTitles(person->Credits)
            : std::vector<::HaloDesktop::Services::SearchTitle>{};
        // Before the reveal the films wait for it. After it they are taken
        // in like a late catalog: filling cards and adding after the last.
        if (m_board.Revealed() && m_revealedQuery == query)
        {
            Refresh();
        }
    }

    void SearchViewModel::ForgetCredits()
    {
        if (m_creditsAsked)
        {
            m_peopleService->CancelFilmography();
        }
        m_creditsQuery.clear();
        m_creditsAsked = false;
        m_credits.reset();
    }

    void SearchViewModel::PlacePeople(PeoplePlace place)
    {
        m_peopleDecided = true;
        m_peoplePlace = PeoplePlace::None;
        if (!m_peopleAnswer)
        {
            return;
        }
        auto const row = ::HaloDesktop::Services::ChoosePeopleRow(*m_peopleAnswer, m_revealedQuery, m_bestTitleTier);
        if (row.empty())
        {
            return;
        }
        std::vector<winrt::Windows::Foundation::IInspectable> items;
        items.reserve(row.size());
        for (auto const& person : row)
        {
            items.push_back(winrt::make<SearchPersonViewModel>(person));
        }
        m_people.ReplaceAll(items);
        m_peoplePlace = place;
    }

    void SearchViewModel::ForgetPeople()
    {
        if (m_people.Size() > 0)
        {
            m_people.Clear();
        }
        m_peoplePlace = PeoplePlace::None;
        m_peopleDecided = false;
    }

    void SearchViewModel::OnFeaturesChanged()
    {
        Raise(L"Placeholder");
        Raise(L"PeopleHintVisibility");
        Raise(L"LinksHintText");
        // A TVDB or IMDb person card that waited for the server's answer.
        if (IsServerLookup() && !m_serverLookupAsked)
        {
            StartServerLookup();
            RaiseAction();
        }
        // So did a magnet card.
        if (IsMagnet() && !m_magnetTracked)
        {
            StartMagnet();
            RaiseAction();
        }
        // A query already under way gets its people now; they land below
        // the results if those are already shown.
        if (m_active && !ActionShown())
        {
            AskPeople(false);
        }
    }

    void SearchViewModel::ComposeAction()
    {
        m_revealTimer.Stop();
        if (m_peopleAsked)
        {
            m_peopleService->CancelSearch();
        }
        m_peopleQuery.clear();
        m_peopleAsked = false;
        m_peopleAnswer.reset();
        ForgetCredits();
        m_board = {};
        m_boardSerial.reset();
        m_absorbed.clear();
        m_hasShown = false;
        ForgetShown();
        m_resultsStale = false;
        m_topMatchSkeleton = false;
        m_resultsSkeleton = false;
        m_stillAskingShown = false;
        m_statusFailed = false;
        m_statusText = {};
        m_noAddons = false;
        m_addonsUnreachable = false;
        m_filteredEmpty = false;
        m_nothingFound = false;
        FinishCompose();
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ActionCardVisibility() const noexcept { return VisibleIf(ActionShown()); }

    bool SearchViewModel::LookupFound() const noexcept
    {
        return m_input.Kind == ::HaloDesktop::Services::SearchInputKind::ImdbTitle
            && m_lookupState == LookupState::Found && m_lookupTitle.has_value();
    }

    bool SearchViewModel::ShowsPoster() const noexcept
    {
        return LookupFound() || ServerTitleFound() || ServerPersonFound() || MagnetShowsPoster();
    }

    bool SearchViewModel::IsMagnet() const noexcept
    {
        return m_input.Kind == ::HaloDesktop::Services::SearchInputKind::Magnet;
    }

    bool SearchViewModel::MagnetShowsPoster() const noexcept
    {
        return IsMagnet() && m_magnetCard.ShowsPoster && m_magnetCard.Identity.has_value();
    }

    void SearchViewModel::SetMagnetPhase(::HaloDesktop::Services::MagnetPhase phase)
    {
        m_magnetPhase = phase;
        m_magnetCard = ::HaloDesktop::Services::BuildMagnetCard({
            .Phase = phase,
            .LinkName = m_input.DisplayName,
            .Request = m_magnetRequest,
        });
    }

    void SearchViewModel::StartMagnet()
    {
        namespace Services = ::HaloDesktop::Services;
        m_magnetRequest.reset();
        m_magnetTracked = false;
        m_features->EnsureLoaded();
        // Until the server says what it offers, the card reads as checking;
        // its answer (OnFeaturesChanged) asks or says it cannot.
        if (!m_features->Settled())
        {
            m_magnets->Stop();
            SetMagnetPhase(Services::MagnetPhase::Checking);
            return;
        }
        // Without an account the features read as none, so this covers
        // Halo without an account too: no magnet call is made.
        if (!m_features->Magnets())
        {
            m_magnets->Stop();
            SetMagnetPhase(Services::MagnetPhase::Unavailable);
            return;
        }
        SetMagnetPhase(Services::MagnetPhase::Checking);
        m_magnetTracked = true;
        // Only the info hash and the display name the classifier kept are
        // sent; never the magnet link or its trackers.
        m_magnets->Track(m_input.InfoHashHex, m_input.DisplayName, [weak = get_weak(), hash = m_input.InfoHashHex](
            Services::MagnetOutcome outcome,
            std::optional<::HaloDesktop::Api::Dto::MagnetRequest> request)
        {
            auto const self = weak.get();
            if (!self || !self->IsMagnet() || self->m_input.InfoHashHex != hash)
            {
                return;
            }
            self->OnMagnetAnswer(outcome, std::move(request));
        });
        UpdateMagnetWatching();
    }

    void SearchViewModel::StopMagnet()
    {
        if (m_magnetTracked)
        {
            m_magnets->Stop();
        }
        m_magnetTracked = false;
        m_magnetRequest.reset();
        UpdateMagnetWatching();
    }

    void SearchViewModel::OnMagnetAnswer(
        ::HaloDesktop::Services::MagnetOutcome outcome,
        std::optional<::HaloDesktop::Api::Dto::MagnetRequest> request)
    {
        namespace Services = ::HaloDesktop::Services;
        switch (outcome)
        {
        case Services::MagnetOutcome::Answered:
            m_magnetRequest = std::move(request);
            SetMagnetPhase(m_magnetRequest ? Services::MagnetPhase::Answered : Services::MagnetPhase::Failed);
            break;
        case Services::MagnetOutcome::NotAvailable:
            m_magnetRequest.reset();
            SetMagnetPhase(Services::MagnetPhase::Unavailable);
            break;
        case Services::MagnetOutcome::Failed:
            // The last good answer is kept for nothing: the card says it
            // could not check, and Try again asks anew.
            m_magnetRequest.reset();
            SetMagnetPhase(Services::MagnetPhase::Failed);
            break;
        }
        RaiseAction();
    }

    void SearchViewModel::UpdateMagnetWatching()
    {
        m_magnets->SetWatching(m_onScreen && IsMagnet() && m_magnetTracked);
    }

    void SearchViewModel::OpenMagnetTitle()
    {
        if (!IsMagnet() || m_magnetCard.Action != ::HaloDesktop::Services::MagnetCardAction::OpenTitle || !m_magnetCard.Identity)
        {
            return;
        }
        auto const& identity = *m_magnetCard.Identity;
        // The normal title page; the magnet waits in its Sources sheet.
        m_navigation->GoTo(
            ::HaloDesktop::Services::Page::Detail,
            winrt::make<winrt::HaloDesktop::implementation::DetailNavParams>(
                identity.Type, identity.Id, identity.Name, identity.Poster.value_or(winrt::hstring{})));
    }

    void SearchViewModel::PlayUnidentifiedMagnet()
    {
        namespace Services = ::HaloDesktop::Services;
        if (!IsMagnet() || m_magnetCard.Action != Services::MagnetCardAction::PlayUnidentified || !m_magnetCard.UnidentifiedFile)
        {
            return;
        }
        auto const& file = *m_magnetCard.UnidentifiedFile;
        // The mapper rebuilt the link on the app's own server; read once more
        // as a web link, as a pasted one is, so nothing else (a local path)
        // can ever reach playback.
        auto const link = Services::ClassifySearchInput(std::wstring_view{ file.PlayUrl });
        if (link.Kind != Services::SearchInputKind::WebLink || link.Url != std::wstring_view{ file.PlayUrl })
        {
            return;
        }
        std::wstring title{ file.Name };
        if (title.size() > Services::SearchWebLinkTitleMaxLength) title.resize(Services::SearchWebLinkTitleMaxLength);
        // As a pasted link: no video id, item id or title page, so nothing is
        // kept in watch history and no addon is asked for subtitles. The
        // link is opened bare; the account's token never goes with it.
        auto const request = winrt::make<winrt::HaloDesktop::implementation::PlaybackRequest>(
            file.PlayUrl,
            false,
            L"",
            L"",
            L"movie",
            L"",
            L"",
            L"",
            winrt::hstring{ title },
            L"",
            L"",
            L"",
            L"",
            L"",
            file.Name,
            file.SizeBytes,
            L"",
            std::vector<std::pair<winrt::hstring, winrt::hstring>>{},
            false);
        m_navigation->ShowOverlay(Services::Page::Player, request);
    }

    std::wstring const& SearchViewModel::ServerLookupId() const noexcept
    {
        return m_input.Kind == ::HaloDesktop::Services::SearchInputKind::TvdbNumeric ? m_input.TvdbId : m_input.PersonId;
    }

    winrt::HaloDesktop::SearchActionTone SearchViewModel::ActionTone() const noexcept
    {
        using Kind = ::HaloDesktop::Services::SearchInputKind;
        switch (m_input.Kind)
        {
        case Kind::ImdbTitle: return m_lookupState == LookupState::NotFound ? SearchActionTone::Mute : SearchActionTone::Accent;
        case Kind::WebLink: return SearchActionTone::Accent;
        case Kind::ImdbPerson:
        case Kind::TvdbNumeric:
            if (m_lookupState == LookupState::Looking || m_lookupState == LookupState::Found) return SearchActionTone::Accent;
            if (m_lookupState == LookupState::Failed) return SearchActionTone::Warn;
            return SearchActionTone::Mute;
        case Kind::Magnet:
            switch (m_magnetCard.Tone)
            {
            case ::HaloDesktop::Services::MagnetCardTone::Accent: return SearchActionTone::Accent;
            case ::HaloDesktop::Services::MagnetCardTone::Warn: return SearchActionTone::Warn;
            case ::HaloDesktop::Services::MagnetCardTone::Bad: return SearchActionTone::Bad;
            case ::HaloDesktop::Services::MagnetCardTone::Mute: break;
            }
            return SearchActionTone::Mute;
        case Kind::TvdbSlug:
        case Kind::UnsupportedLink:
        case Kind::Text: break;
        }
        return SearchActionTone::Mute;
    }

    winrt::HaloDesktop::SearchActionGlyph SearchViewModel::ActionGlyph() const noexcept
    {
        using Kind = ::HaloDesktop::Services::SearchInputKind;
        if (ShowsPoster())
        {
            return SearchActionGlyph::Poster;
        }
        switch (m_input.Kind)
        {
        case Kind::WebLink:
        case Kind::UnsupportedLink: return SearchActionGlyph::Link;
        case Kind::Magnet: return SearchActionGlyph::Magnet;
        case Kind::ImdbTitle:
        case Kind::ImdbPerson:
        case Kind::TvdbNumeric:
        case Kind::TvdbSlug:
        case Kind::Text: break;
        }
        return SearchActionGlyph::Id;
    }

    winrt::hstring SearchViewModel::ActionKicker() const
    {
        using Kind = ::HaloDesktop::Services::SearchInputKind;
        switch (m_input.Kind)
        {
        case Kind::ImdbTitle:
        case Kind::ImdbPerson: return L"IMDB LINK";
        case Kind::TvdbNumeric:
        case Kind::TvdbSlug: return L"TVDB LINK";
        case Kind::Magnet: return L"MAGNET LINK";
        case Kind::WebLink: return L"VIDEO LINK";
        case Kind::UnsupportedLink: return L"LINK";
        case Kind::Text: break;
        }
        return {};
    }

    winrt::hstring SearchViewModel::ActionTitle() const
    {
        using Kind = ::HaloDesktop::Services::SearchInputKind;
        switch (m_input.Kind)
        {
        case Kind::ImdbTitle:
            if (LookupFound()) return winrt::hstring{ m_lookupTitle->Name };
            if (m_lookupState == LookupState::NotFound) return NoAddonKnowsTitleText;
            return winrt::hstring{ std::wstring{ LookingUpPrefix } + m_input.ImdbId + L"…" };
        case Kind::WebLink: return winrt::hstring{ m_input.DisplayName.empty() ? m_input.Host : m_input.DisplayName };
        case Kind::Magnet: return winrt::hstring{ m_magnetCard.Title };
        case Kind::TvdbNumeric:
        case Kind::ImdbPerson:
            if (ServerTitleFound() || ServerPersonFound()) return m_idLookup->Name;
            if (m_lookupState == LookupState::NotFound)
            {
                return m_input.Kind == Kind::TvdbNumeric ? ServerUnknownTitleText : ServerUnknownPersonText;
            }
            if (m_lookupState == LookupState::Unsupported || m_lookupState == LookupState::Failed) return CannotLookUpText;
            return winrt::hstring{ std::wstring{ LookingUpPrefix } + ServerLookupId() + L"…" };
        case Kind::TvdbSlug: return CannotLookUpText;
        case Kind::UnsupportedLink: return CannotPlayText;
        case Kind::Text: break;
        }
        return {};
    }

    winrt::hstring SearchViewModel::ActionRating() const
    {
        if (!LookupFound()) return {};
        return winrt::hstring{ ::HaloDesktop::Services::RatingLabel(View(m_lookupTitle->Rating)).value_or(L"") };
    }

    winrt::hstring SearchViewModel::ActionMono() const
    {
        if (LookupFound()) return m_lookupTitle->Type == L"series" ? L"SERIES" : L"MOVIE";
        if (ServerTitleFound()) return m_idLookup->Type == L"series" ? L"SERIES" : L"MOVIE";
        if (m_input.Kind == ::HaloDesktop::Services::SearchInputKind::WebLink) return winrt::hstring{ m_input.Host };
        if (IsMagnet()) return winrt::hstring{ m_magnetCard.Mono };
        return {};
    }

    winrt::hstring SearchViewModel::ActionYear() const
    {
        if (IsMagnet()) return winrt::hstring{ m_magnetCard.Plain };
        if (ServerTitleFound()) return m_idLookup->Year.value_or(winrt::hstring{});
        if (!LookupFound()) return {};
        return winrt::hstring{ m_lookupTitle->ReleaseInfo.value_or(L"") };
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ActionMetaVisibility() const noexcept
    {
        try { return VisibleIf(!ActionRating().empty() || !ActionMono().empty() || !ActionYear().empty() || !ActionTail().empty()); } catch (...) { return Collapsed; }
    }

    winrt::hstring SearchViewModel::ActionTail() const
    {
        return IsMagnet() ? winrt::hstring{ m_magnetCard.Tail } : winrt::hstring{};
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ActionTailVisibility() const noexcept
    {
        return VisibleIf(IsMagnet() && !m_magnetCard.Tail.empty());
    }

    double SearchViewModel::ActionProgress() const noexcept
    {
        return IsMagnet() && m_magnetCard.Progress ? *m_magnetCard.Progress * 100.0 : 0.0;
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ActionProgressVisibility() const noexcept
    {
        return VisibleIf(IsMagnet() && m_magnetCard.Progress.has_value());
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ActionRatingVisibility() const noexcept
    {
        try { return VisibleIf(!ActionRating().empty()); } catch (...) { return Collapsed; }
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ActionMonoVisibility() const noexcept
    {
        try { return VisibleIf(!ActionMono().empty()); } catch (...) { return Collapsed; }
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ActionYearVisibility() const noexcept
    {
        try { return VisibleIf(!ActionYear().empty()); } catch (...) { return Collapsed; }
    }

    winrt::hstring SearchViewModel::ActionSecondary() const
    {
        using Kind = ::HaloDesktop::Services::SearchInputKind;
        switch (m_input.Kind)
        {
        case Kind::ImdbTitle:
            if (LookupFound()) return {};
            if (m_lookupState == LookupState::NotFound) return winrt::hstring{ m_input.ImdbId + NotFoundSuffix };
            return LookingUpSecondary;
        case Kind::WebLink: return WebLinkSecondary;
        case Kind::TvdbSlug: return TvdbSlugText;
        case Kind::UnsupportedLink: return winrt::hstring{ m_input.Reason };
        case Kind::ImdbPerson:
        case Kind::TvdbNumeric:
            switch (m_lookupState)
            {
            case LookupState::Unsupported: return NeedsServerText;
            case LookupState::Failed: return ServerNoAnswerText;
            case LookupState::NotFound: return winrt::hstring{ ServerLookupId() + ServerNotFoundSuffix };
            case LookupState::None:
            case LookupState::Looking: return m_input.Kind == Kind::TvdbNumeric ? AskingServerTitleText : AskingServerPersonText;
            case LookupState::Found: break;
            }
            return {};
        case Kind::Magnet: return winrt::hstring{ m_magnetCard.Secondary };
        case Kind::Text: break;
        }
        return {};
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ActionSecondaryVisibility() const noexcept
    {
        try { return VisibleIf(!ActionSecondary().empty()); } catch (...) { return Collapsed; }
    }

    // A found person leads with their photo in the poster's frame.
    winrt::hstring SearchViewModel::ActionPoster() const
    {
        if (MagnetShowsPoster()) return m_magnetCard.Identity->Poster.value_or(winrt::hstring{});
        if (ServerTitleFound()) return m_idLookup->Poster.value_or(winrt::hstring{});
        if (ServerPersonFound()) return m_idLookup->Photo.value_or(winrt::hstring{});
        if (!LookupFound() || ::HaloDesktop::Services::HasNoPoster(*m_lookupTitle)) return {};
        return winrt::hstring{ *m_lookupTitle->Poster };
    }

    winrt::hstring SearchViewModel::ActionMetaId() const
    {
        if (MagnetShowsPoster()) return m_magnetCard.Identity->Id;
        if (ServerTitleFound()) return m_idLookup->Id;
        return LookupFound() ? winrt::hstring{ m_lookupTitle->Id } : winrt::hstring{};
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ActionPosterVisibility() const noexcept
    {
        return VisibleIf(ShowsPoster());
    }

    winrt::hstring SearchViewModel::ActionButtonLabel() const
    {
        if (m_input.Kind == ::HaloDesktop::Services::SearchInputKind::WebLink) return PlayLinkText;
        if (ServerPersonFound()) return OpenPersonText;
        return OpenTitleText;
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ActionButtonVisibility() const noexcept
    {
        return VisibleIf(m_input.Kind == ::HaloDesktop::Services::SearchInputKind::WebLink
            || LookupFound()
            || ServerTitleFound()
            || (ServerPersonFound() && PeopleAvailable())
            || (IsMagnet() && m_magnetCard.Action == ::HaloDesktop::Services::MagnetCardAction::OpenTitle));
    }

    winrt::hstring SearchViewModel::ActionPlainButtonLabel() const
    {
        return IsMagnet() ? winrt::hstring{ m_magnetCard.ActionLabel } : winrt::hstring{};
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ActionPlainButtonVisibility() const noexcept
    {
        using Action = ::HaloDesktop::Services::MagnetCardAction;
        return VisibleIf(IsMagnet() && m_magnetCard.Action != Action::None && m_magnetCard.Action != Action::OpenTitle);
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ActionPlainButtonGlyphVisibility() const noexcept
    {
        return VisibleIf(IsMagnet() && m_magnetCard.Action == ::HaloDesktop::Services::MagnetCardAction::PlayUnidentified);
    }

    void SearchViewModel::ActivatePlainAction()
    {
        namespace Services = ::HaloDesktop::Services;
        if (!IsMagnet())
        {
            return;
        }
        switch (m_magnetCard.Action)
        {
        case Services::MagnetCardAction::Cancel:
            m_magnets->Forget();
            // Still handled: a later answer about the server's features must
            // not ask about it again by itself.
            m_magnetTracked = true;
            m_magnetRequest.reset();
            SetMagnetPhase(Services::MagnetPhase::Stopped);
            UpdateMagnetWatching();
            RaiseAction();
            return;
        case Services::MagnetCardAction::Retry:
            StartMagnet();
            RaiseAction();
            return;
        case Services::MagnetCardAction::PlayUnidentified:
            PlayUnidentifiedMagnet();
            return;
        case Services::MagnetCardAction::OpenTitle:
        case Services::MagnetCardAction::None:
            return;
        }
    }

    Microsoft::UI::Xaml::Visibility SearchViewModel::ActionButtonGlyphVisibility() const noexcept
    {
        return VisibleIf(m_input.Kind == ::HaloDesktop::Services::SearchInputKind::WebLink);
    }

    void SearchViewModel::ActivateAction()
    {
        using Kind = ::HaloDesktop::Services::SearchInputKind;
        if (m_input.Kind == Kind::ImdbTitle)
        {
            OpenFoundTitle();
        }
        else if (m_input.Kind == Kind::WebLink)
        {
            PlayPastedLink();
        }
        else if (IsServerLookup())
        {
            OpenServerLookup();
        }
        else if (IsMagnet())
        {
            // Only Open title; the card's other buttons act by a click on them.
            OpenMagnetTitle();
        }
    }

    void SearchViewModel::OpenFoundTitle()
    {
        if (m_lookupState != LookupState::Found || !m_lookupTitle)
        {
            return;
        }
        auto const& title = *m_lookupTitle;
        auto const poster = ::HaloDesktop::Services::HasNoPoster(title) ? std::wstring{} : title.Poster.value_or(L"");
        m_navigation->GoTo(
            ::HaloDesktop::Services::Page::Detail,
            winrt::make<winrt::HaloDesktop::implementation::DetailNavParams>(
                winrt::hstring{ title.Type }, winrt::hstring{ title.Id }, winrt::hstring{ title.Name }, winrt::hstring{ poster }));
    }

    void SearchViewModel::PlayPastedLink()
    {
        namespace Services = ::HaloDesktop::Services;
        // Read again from the box rather than trusted from the card: only a
        // web link the classifier accepts, exactly as shown, reaches playback.
        auto const input = Services::ClassifySearchInput(Services::TrimmedText(m_query));
        if (input.Kind != Services::SearchInputKind::WebLink || input != m_input || input.Url.empty())
        {
            return;
        }
        // No video id, item id or title page: nothing about the link is kept
        // in watch history, and no addon is asked for its subtitles.
        auto const request = winrt::make<winrt::HaloDesktop::implementation::PlaybackRequest>(
            winrt::hstring{ input.Url },
            false,
            L"",
            L"",
            L"movie",
            L"",
            L"",
            L"",
            winrt::hstring{ input.DisplayName },
            L"",
            L"",
            L"",
            L"",
            L"",
            L"",
            0,
            L"",
            std::vector<std::pair<winrt::hstring, winrt::hstring>>{},
            false);
        m_navigation->ShowOverlay(Services::Page::Player, request);
    }

    void SearchViewModel::RaiseAction()
    {
        for (auto const name : {
                 L"ActionCardVisibility", L"ActionTone", L"ActionGlyph", L"ActionKicker", L"ActionTitle",
                 L"ActionRating", L"ActionMono", L"ActionYear", L"ActionMetaVisibility", L"ActionRatingVisibility",
                 L"ActionMonoVisibility", L"ActionYearVisibility", L"ActionSecondary", L"ActionSecondaryVisibility",
                 L"ActionPoster", L"ActionMetaId", L"ActionPosterVisibility", L"ActionButtonLabel",
                 L"ActionButtonVisibility", L"ActionButtonGlyphVisibility", L"ActionTail", L"ActionTailVisibility",
                 L"ActionProgress", L"ActionProgressVisibility", L"ActionPlainButtonLabel",
                 L"ActionPlainButtonVisibility", L"ActionPlainButtonGlyphVisibility" })
        {
            Raise(name);
        }
    }

    void SearchViewModel::FinishCompose()
    {
        m_topMatchInLibrary = m_topMatchTitle && m_search->InLibrary(m_topMatchTitle->Type, m_topMatchTitle->Id);
        m_recentShown = !m_active && !ActionShown() && m_recent.Size() > 0;
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
                 L"TopMatchPoster", L"TopMatchMetaId", L"TopMatchBackdrop", L"TopMatchSoftBackdrop",
                 L"TopMatchRatingVisibility", L"TopMatchYearVisibility", L"TopMatchInLibraryVisibility", L"TopMatchBlurbVisibility",
                 L"RecentVisibility", L"HintsVisibility", L"StatusVisibility", L"StatusFailed", L"NoAddonsVisibility", L"TopMatchVisibility", L"TopMatchSkeletonVisibility",
                 L"LibraryVisibility", L"ResultsVisibility", L"ResultsSkeletonVisibility",
                 L"FilteredEmptyVisibility", L"NothingFoundVisibility", L"AddonsUnreachableVisibility",
                 L"PeopleAboveVisibility", L"PeopleBelowVisibility", L"PeopleHintVisibility", L"LinksHintText", L"Placeholder" })
        {
            Raise(name);
        }
        RaiseAction();
    }
}
