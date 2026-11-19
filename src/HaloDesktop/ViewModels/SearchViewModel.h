#pragma once
#include "RecentSearchViewModel.g.h"
#include "SearchViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/SearchInputPolicy.h"
#include "Services/SearchResultBoard.h"
#include "Services/SearchResultsPolicy.h"
#include "Services/SearchService.h"

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
        // card that only says something.
        void ActivateAction();

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
        // Arriving with Home's text: searched at once, not recorded.
        void Arrive(winrt::hstring const& query);
        void Shown();
        // Keeps the query among the recent terms, which opening any of its
        // results does, from the poster menu too.
        void RecordQuery();

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        // Where the IMDb card's lookup stands.
        enum class LookupState
        {
            None,
            Looking,
            Found,
            NotFound,
        };

        void SetQueryText(winrt::hstring const& value);
        // Reads the box. For anything but words it shows the action card,
        // stops any catalog search and returns true; the caller then does
        // nothing more with the text.
        [[nodiscard]] bool ApplyInput();
        [[nodiscard]] bool ActionShown() const noexcept;
        // The IMDb card's title was found, so the card leads with its poster.
        [[nodiscard]] bool LookupFound() const noexcept;
        void StartTitleLookup();
        // While the action card is up: no results, no status line.
        void ComposeAction();
        void RaiseAction();
        // Keeps `term` among the recent searches only when it is words.
        void RecordIfText(std::wstring_view term);
        void OpenFoundTitle();
        void PlayPastedLink();
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
        void ApplyDiff(::HaloDesktop::Services::BoardDiff const& diff);
        void OnRevealTick();
        [[nodiscard]] bool RevealIsDue(
            std::vector<::HaloDesktop::Services::SearchTargetState> const& states,
            ::HaloDesktop::Services::SearchProgressResult const& progress) const;
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
            m_suggestions{ nullptr }, m_recent{ nullptr }, m_library{ nullptr }, m_results{ nullptr };
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
