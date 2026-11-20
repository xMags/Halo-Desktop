#pragma once
#include "RecentSearchViewModel.g.h"
#include "SearchPersonViewModel.g.h"
#include "SearchViewModel.g.h"
#include "Api/Dto.h"
#include "Services/AppServices.h"
#include "Services/MagnetCardPolicy.h"
#include "Services/MagnetService.h"
#include "Services/SearchInputPolicy.h"
#include "Services/SearchResultBoard.h"
#include "Services/SearchResultsPolicy.h"
#include "Services/PeopleService.h"
#include "Services/SearchService.h"
#include "Services/ServerFeaturesService.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    struct RecentSearchViewModel : RecentSearchViewModelT<RecentSearchViewModel>
    {
        explicit RecentSearchViewModel(winrt::hstring term);
        [[nodiscard]] winrt::hstring Term() const;
        [[nodiscard]] winrt::hstring RemoveLabel() const;

    private:
        winrt::hstring m_term;
    };

    // One person in Search's people row.
    struct SearchPersonViewModel : SearchPersonViewModelT<SearchPersonViewModel>
    {
        explicit SearchPersonViewModel(::HaloDesktop::Api::Dto::PersonSummary const& person);
        [[nodiscard]] winrt::hstring Name() const { return m_name; }
        [[nodiscard]] winrt::hstring Line() const { return m_line; }
        [[nodiscard]] winrt::hstring Role() const { return m_role; }
        [[nodiscard]] winrt::Windows::Foundation::IInspectable LineToolTip() const;
        [[nodiscard]] winrt::hstring Photo() const { return m_photo; }
        [[nodiscard]] winrt::hstring Initials() const { return m_initials; }
        [[nodiscard]] std::int64_t TmdbId() const noexcept { return m_tmdbId; }

    private:
        std::int64_t m_tmdbId{};
        winrt::hstring m_name;
        winrt::hstring m_line;
        winrt::hstring m_role;
        winrt::hstring m_photo;
        winrt::hstring m_initials;
    };

    // The Search page's state: the box and its suggestions, recent searches
    // and what the box accepts before a search, and during one the status line, the top match, the
    // library's matches and the results.
    //
    // A query's results are revealed once (SearchRevealPolicy decides when),
    // ranked by SearchRankingPolicy and held by a SearchResultBoard. Until
    // then the previous query's results stay on screen, dimmed. After it, an
    // answer that lands late only fills in cards in place or adds cards after
    // the last one; only the user (a new query, a kind chip, clearing the box)
    // lays the grid out again.
    //
    // On a server with people search, a query also asks who matches it. The
    // people row always comes first, above the top match and the results. The
    // reveal waits a while for its answer; one that lands later still goes on
    // top, moving what is shown down once. Without the feature (or an account)
    // no people call is made and nothing about people is shown.
    //
    // UI-thread only. Two timers (the typing pause, and the reveal check while
    // a query is unrevealed), both stopped on destruction; the service and the
    // timers reach this only through weak references.
    struct SearchViewModel : SearchViewModelT<SearchViewModel>
    {
        explicit SearchViewModel(::HaloDesktop::Services::AppServices const& services);
        ~SearchViewModel();
        SearchViewModel(SearchViewModel const&) = delete;
        SearchViewModel& operator=(SearchViewModel const&) = delete;

        [[nodiscard]] winrt::hstring Query() const;
        void Query(winrt::hstring const& value);
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ClearVisibility() const noexcept;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Suggestions() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable RecentItems() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable LibraryItems() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable ResultItems() const;
        auto SuggestionsView() const { return m_suggestions; }
        auto RecentItemsView() const { return m_recent; }
        auto LibraryItemsView() const { return m_library; }
        auto ResultItemsView() const { return m_results; }
        [[nodiscard]] winrt::Windows::Foundation::IInspectable PeopleItems() const;
        auto PeopleItemsView() const { return m_people; }
        // The people row, first of everything a query shows (see the class comment).
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PeopleVisibility() const noexcept;
        // The legend's people item, and the box's words, follow the server.
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PeopleHintVisibility() const noexcept;
        // The legend's link item names magnets only when the server opens them.
        [[nodiscard]] winrt::hstring LinksHintText() const;
        [[nodiscard]] winrt::hstring Placeholder() const;
        [[nodiscard]] bool ShowKindBadges() const noexcept;
        [[nodiscard]] winrt::hstring ResultCountText() const;
        // What the status line under the chips says: catalogs still out, or
        // the ones that did not answer.
        [[nodiscard]] winrt::hstring StatusText() const;
        [[nodiscard]] bool StillAsking() const noexcept;
        // Whether what is on screen belongs to an earlier query, still shown
        // while this one's answers are awaited.
        [[nodiscard]] bool ResultsStale() const noexcept;
        [[nodiscard]] winrt::hstring FilteredEmptyText() const;

        [[nodiscard]] winrt::HaloDesktop::MediaSummary TopMatch() const;
        [[nodiscard]] winrt::hstring TopMatchTitle() const;
        [[nodiscard]] winrt::hstring TopMatchRating() const;
        [[nodiscard]] winrt::hstring TopMatchKind() const;
        [[nodiscard]] winrt::hstring TopMatchYear() const;
        [[nodiscard]] winrt::hstring TopMatchBlurb() const;
        [[nodiscard]] winrt::hstring TopMatchPoster() const;
        [[nodiscard]] winrt::hstring TopMatchMetaId() const;
        [[nodiscard]] winrt::hstring TopMatchBackdrop() const;
        [[nodiscard]] winrt::hstring TopMatchSoftBackdrop() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopMatchRatingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopMatchYearVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopMatchInLibraryVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopMatchBlurbVisibility() const noexcept;

        [[nodiscard]] Microsoft::UI::Xaml::Visibility SuggestionsVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RecentVisibility() const noexcept;
        // The legend of what the box accepts, while nothing is typed.
        [[nodiscard]] Microsoft::UI::Xaml::Visibility HintsVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility NoAddonsVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopMatchVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopMatchSkeletonVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LibraryVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ResultsVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ResultsSkeletonVisibility() const noexcept;
        // Shown whenever a query is active, so its height never comes and goes.
        [[nodiscard]] Microsoft::UI::Xaml::Visibility StatusVisibility() const noexcept;
        // Whether the status line names catalogs that did not answer, which
        // it says in the warning colour and without the ring.
        [[nodiscard]] bool StatusFailed() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FilteredEmptyVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility NothingFoundVisibility() const noexcept;
        // No catalog could be asked, or every one asked failed, and nothing is shown.
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AddonsUnreachableVisibility() const noexcept;
        // Whether any skeleton is drawn, for the page's pulse.
        [[nodiscard]] bool SkeletonsShown() const noexcept;

        // The one card shown in the top match's place while the box holds a
        // link or an id instead of words: what it is, and what Enter or a
        // click on it does.
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionCardVisibility() const noexcept;
        [[nodiscard]] winrt::HaloDesktop::SearchActionTone ActionTone() const noexcept;
        [[nodiscard]] winrt::HaloDesktop::SearchActionGlyph ActionGlyph() const noexcept;
        [[nodiscard]] winrt::hstring ActionKicker() const;
        [[nodiscard]] winrt::hstring ActionTitle() const;
        // The meta row under the title: a rating, a value in mono (the kind,
        // or the link's host) and a year, each shown only when it has text.
        [[nodiscard]] winrt::hstring ActionRating() const;
        [[nodiscard]] winrt::hstring ActionMono() const;
        [[nodiscard]] winrt::hstring ActionYear() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionMetaVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionRatingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionMonoVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionYearVisibility() const noexcept;
        // A second value in mono at the end of the meta row (a size, a percentage).
        [[nodiscard]] winrt::hstring ActionTail() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionTailVisibility() const noexcept;
        // A download's progress, 0 to 100, while the server fetches a magnet.
        [[nodiscard]] double ActionProgress() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionProgressVisibility() const noexcept;
        // The quieter sentence under the title.
        [[nodiscard]] winrt::hstring ActionSecondary() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionSecondaryVisibility() const noexcept;
        [[nodiscard]] winrt::hstring ActionPoster() const;
        [[nodiscard]] winrt::hstring ActionMetaId() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionPosterVisibility() const noexcept;
        [[nodiscard]] winrt::hstring ActionButtonLabel() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionButtonVisibility() const noexcept;
        // The play glyph, on the one button that plays.
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionButtonGlyphVisibility() const noexcept;
        // Opens the IMDb title's page or plays the pasted link; nothing for a
        // card that only says something. Enter, a click on the card and its
        // accent button all come here.
        void ActivateAction();
        // The magnet card's standard button (Cancel, Try again, Play as
        // unidentified video): only by a click on it, never by Enter.
        [[nodiscard]] winrt::hstring ActionPlainButtonLabel() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionPlainButtonVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionPlainButtonGlyphVisibility() const noexcept;
        void ActivatePlainAction();

        void SetFilter(std::int32_t index);
        void SetBoxFocused(bool focused);
        // Enter: searches now and keeps the term.
        void Submit();
        // Esc and the box's clear button.
        void ClearQuery();
        // A recent pill or a suggestion: searches it now and keeps it.
        void SearchAgain(winrt::hstring const& term);
        void Forget(winrt::hstring const& term);
        void ForgetAll();
        void ShowAllResults();
        // "Try again" under "Search could not reach your addons".
        void RetrySearch();
        // "Open Settings" under "None of your addons can search".
        void OpenSettings();
        // Opening a result keeps the query among the recent terms.
        void OpenDetail(winrt::Windows::Foundation::IInspectable const& item);
        void OpenTopMatch();
        // A card of the people row: that person's page.
        void OpenPerson(winrt::Windows::Foundation::IInspectable const& item);
        // Arriving with Home's text: searched at once, not recorded.
        void Arrive(winrt::hstring const& query);
        void Shown();
        // The page left the screen: a magnet download is no longer followed.
        void Hidden();
        // Keeps the query among the recent terms, which opening any of its
        // results does, from the poster menu too.
        void RecordQuery();

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        // Where the action card's lookup stands: the IMDb title card's, asked
        // of the addons, or the TVDB and IMDb person cards', asked of the server.
        enum class LookupState
        {
            None,
            Looking,
            Found,
            NotFound,
            // The server could not answer (server lookups only).
            Failed,
            // The server cannot look these up (server lookups only).
            Unsupported,
        };

        void SetQueryText(winrt::hstring const& value);
        // Reads the box. For anything but words it shows the action card,
        // stops any catalog search and returns true; the caller then does
        // nothing more with the text.
        [[nodiscard]] bool ApplyInput();
        [[nodiscard]] bool ActionShown() const noexcept;
        // The IMDb card's title was found, so the card leads with its poster.
        [[nodiscard]] bool LookupFound() const noexcept;
        // The card leads with a picture: a found title's poster or a found person's photo.
        [[nodiscard]] bool ShowsPoster() const noexcept;
        // The TVDB number or IMDb person id a server lookup card is about.
        [[nodiscard]] std::wstring const& ServerLookupId() const noexcept;
        void StartTitleLookup();
        // A TVDB number or an IMDb person: asked of the server once its
        // features are known; the card says so when it cannot.
        [[nodiscard]] bool IsServerLookup() const noexcept;
        void StartServerLookup();
        [[nodiscard]] bool ServerTitleFound() const noexcept;
        [[nodiscard]] bool ServerPersonFound() const noexcept;
        void OpenServerLookup();
        // People search for this query, beside the catalogs, when the server has it.
        [[nodiscard]] bool PeopleAvailable() const noexcept;
        void AskPeople(bool retry);
        void OnPeopleAnswer(std::wstring const& query, ::HaloDesktop::Services::PeopleOutcome outcome, ::HaloDesktop::Api::Dto::PeopleSearch answer);
        // The names found for what the box holds, for the suggestions under it.
        [[nodiscard]] std::vector<std::wstring> FoundNames() const;
        // The films of the person a query names exactly, as one more answer
        // beside the catalogs'.
        void AskCredits(std::wstring const& query, std::int64_t tmdbId);
        void OnCredits(std::wstring const& query, std::optional<::HaloDesktop::Api::Dto::PersonDetail> person);
        void ForgetCredits();
        // Places the people row for the revealed query, once.
        void PlacePeople();
        void ForgetPeople();
        void OnFeaturesChanged();
        // While the action card is up: no results, no status line.
        void ComposeAction();
        void RaiseAction();
        // Keeps `term` among the recent searches only when it is words.
        void RecordIfText(std::wstring_view term);
        void OpenFoundTitle();
        void PlayPastedLink();
        // A pasted magnet: asked of the server once its features are known;
        // the card says so when it cannot be.
        [[nodiscard]] bool IsMagnet() const noexcept;
        void StartMagnet();
        void StopMagnet();
        void OnMagnetAnswer(::HaloDesktop::Services::MagnetOutcome outcome, std::optional<::HaloDesktop::Api::Dto::MagnetRequest> request);
        void SetMagnetPhase(::HaloDesktop::Services::MagnetPhase phase);
        // Polling runs only while this page is on screen with the magnet card.
        void UpdateMagnetWatching();
        [[nodiscard]] bool MagnetShowsPoster() const noexcept;
        void OpenMagnetTitle();
        void PlayUnidentifiedMagnet();
        // What the active query stands for: a person's exact name once the
        // people answer says so, else a title's.
        [[nodiscard]] ::HaloDesktop::Services::SearchQueryMeaning QueryMeaning(std::wstring const& query) const;
        // Asks about the first titles' poster links and says whether every
        // answer is in (or the wait for them is over).
        [[nodiscard]] bool PostersSettled(
            std::vector<::HaloDesktop::Services::SearchTargetState> const& states,
            std::chrono::milliseconds sinceAsked);
        void SearchNow();
        // Brings the page to where the service's answers stand.
        void Refresh();
        // Before a query: recent searches and the hints.
        void ComposeIdle();
        // A query asked and not yet revealed: what was shown stays, dimmed.
        void ComposePending(::HaloDesktop::Services::SearchProgressResult const& progress);
        // Lays this query's results out, once.
        void RevealNow(
            std::vector<::HaloDesktop::Services::SearchTargetState> const& states,
            ::HaloDesktop::Services::SearchProgressResult const& progress);
        // A revealed query: answers that landed since go to the board.
        void AbsorbLate(std::vector<::HaloDesktop::Services::SearchTargetState> const& states);
        // Every catalog answer offered to the board again, after its rules
        // let more in (a person's name known only after the reveal).
        void ReofferAnswers();
        void MarkMissingPosters(std::vector<::HaloDesktop::Services::SearchAnswer>& answers) const;
        void ApplyDiff(::HaloDesktop::Services::BoardDiff const& diff);
        void OnRevealTick();
        [[nodiscard]] bool RevealIsDue(
            std::vector<::HaloDesktop::Services::SearchTargetState> const& states,
            ::HaloDesktop::Services::SearchProgressResult const& progress);
        void StartBoard(std::uint64_t serial);
        // Empties the results, the library row and the top match.
        void ForgetShown();
        // The board's titles that the kind chip and the library row leave to
        // the grid, as one reset of it (a user action).
        void ShowSubsequence();
        [[nodiscard]] bool ShownInGrid(::HaloDesktop::Services::SearchTitle const& title) const;
        // The status line, the empty states and the placeholders of a revealed query.
        void ComposeRevealed(::HaloDesktop::Services::SearchProgressResult const& progress);
        void SetTopMatch(std::optional<::HaloDesktop::Services::SearchTitle> title);
        void FinishCompose();
        // The answers among `states` with titles; with `onlyNew`, only those
        // the board has not had yet. Every settled answer is marked as had.
        [[nodiscard]] std::vector<::HaloDesktop::Services::SearchAnswer> AnswersFrom(
            std::vector<::HaloDesktop::Services::SearchTargetState> const& states,
            bool onlyNew);
        void RebuildHistoryLists();
        [[nodiscard]] std::optional<std::wstring> FilterType() const;
        void Raise(wchar_t const* name);
        void RaiseAll();

        std::shared_ptr<::HaloDesktop::Services::SearchService> m_search;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        std::shared_ptr<::HaloDesktop::Services::TitleLookupService> m_titleLookup;
        std::shared_ptr<::HaloDesktop::Services::ServerFeaturesService> m_features;
        std::shared_ptr<::HaloDesktop::Services::PeopleService> m_peopleService;
        std::shared_ptr<::HaloDesktop::Services::PosterCheckService> m_posterChecks;
        std::shared_ptr<::HaloDesktop::Services::MagnetService> m_magnets;
        ::HaloDesktop::Services::ServerFeaturesChangedToken m_featuresToken{};
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_debounceTimer{ nullptr };
        // Runs only while the current query is asked and not yet revealed.
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_revealTimer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        ::HaloDesktop::Services::SearchChangedToken m_searchToken{};
        std::uint64_t m_accountVersion{};

        winrt::hstring m_query;
        // The query the service was last asked, before trimming.
        std::wstring m_activeQuery;
        std::int32_t m_filterIndex{};
        bool m_boxFocused{};
        // What the box holds, read on every change.
        ::HaloDesktop::Services::SearchInput m_input;
        LookupState m_lookupState{ LookupState::None };
        std::optional<::HaloDesktop::Services::SearchTitle> m_lookupTitle;
        // The server's answer for a TVDB number or an IMDb person.
        std::optional<::HaloDesktop::Api::Dto::IdLookup> m_idLookup;
        // Whether that lookup was asked of the server (not only waiting for
        // the server's features).
        bool m_serverLookupAsked{};

        // The pasted magnet: where it stands, the server's last answer, and
        // the card both make (MagnetCardPolicy).
        ::HaloDesktop::Services::MagnetPhase m_magnetPhase{ ::HaloDesktop::Services::MagnetPhase::Checking };
        std::optional<::HaloDesktop::Api::Dto::MagnetRequest> m_magnetRequest;
        ::HaloDesktop::Services::MagnetCard m_magnetCard;
        // Whether the magnet was handed to the service (not only waiting for
        // the server's features).
        bool m_magnetTracked{};
        // Between Shown and Hidden.
        bool m_onScreen{};

        // The people asked for the query `m_peopleQuery` (trimmed): asked,
        // and the answer once it is in (an empty list when nobody, or failed).
        std::wstring m_peopleQuery;
        bool m_peopleAsked{};
        std::optional<std::vector<::HaloDesktop::Api::Dto::PersonSummary>> m_peopleAnswer;
        bool m_peopleFailed{};
        // The name the people answer is read by: the query, or the name the
        // server corrected a misspelling to (m_peopleCorrected).
        std::wstring m_peopleName;
        bool m_peopleCorrected{};
        // The films of the person `m_creditsQuery` (trimmed) names exactly:
        // asked, and the titles once the answer is in (none when it failed).
        std::wstring m_creditsQuery;
        bool m_creditsAsked{};
        std::optional<std::vector<::HaloDesktop::Services::SearchTitle>> m_credits;
        // The query the board revealed (trimmed), and its best title tier,
        // frozen at the reveal so a late people answer is judged the same way.
        std::wstring m_revealedQuery;
        std::optional<int> m_bestTitleTier;
        bool m_peopleDecided{};
        // The revealed query's people row has someone to show.
        bool m_peopleShown{};

        // The current query's results, from the reveal on.
        ::HaloDesktop::Services::SearchResultBoard m_board;
        // The service serial the board belongs to; notifications for another are not its.
        std::optional<std::uint64_t> m_boardSerial;
        std::chrono::steady_clock::time_point m_askedAt{};
        // Targets whose answer the board has had, by key.
        std::unordered_set<std::wstring> m_absorbed;
        // The library's matches at the reveal, before the kind chip.
        std::vector<::HaloDesktop::Services::SearchTitle> m_libraryAll;
        // Identities of what the library row shows, which the grid leaves out.
        std::unordered_set<std::wstring> m_libraryIdentities;
        // Board indices of the grid's cards, ascending.
        std::vector<std::size_t> m_displayed;
        // Whether some query's results are on screen, since the box was last idle.
        bool m_hasShown{};

        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable>
            m_suggestions{ nullptr }, m_recent{ nullptr }, m_library{ nullptr }, m_results{ nullptr }, m_people{ nullptr };
        // What each collection holds, so an answer that changes nothing leaves
        // the realised cards alone.
        std::vector<::HaloDesktop::Services::SearchTitle> m_libraryShown;
        std::vector<std::wstring> m_recentTerms, m_suggestionTerms;

        std::optional<::HaloDesktop::Services::SearchTitle> m_topMatchTitle;
        winrt::HaloDesktop::MediaSummary m_topMatch{ nullptr };
        bool m_topMatchInLibrary{};
        winrt::hstring m_statusText;
        std::size_t m_resultCount{};
        bool m_resultsStale{};
        bool m_active{};
        bool m_suggestionsShown{};
        // Enter or a picked suggestion puts the list away until the next keystroke.
        bool m_suggestionsDismissed{};
        bool m_recentShown{};
        bool m_noAddons{};
        bool m_topMatchSkeleton{};
        bool m_resultsSkeleton{};
        bool m_stillAskingShown{};
        bool m_statusFailed{};
        bool m_addonsUnreachable{};
        bool m_filteredEmpty{};
        bool m_nothingFound{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
