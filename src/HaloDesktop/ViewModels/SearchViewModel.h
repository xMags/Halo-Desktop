#pragma once
#include "RecentSearchViewModel.g.h"
#include "SearchViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/SearchResultsPolicy.h"
#include "Services/SearchService.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
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

    // The Search page's state, as the reference's `search/Search.tsx` lays it
    // out: the box and its suggestions, recent and popular before a search,
    // and during one the notes, the top match, the library's matches and the
    // merged results. What is on screen is recomposed from the search
    // service's answers whenever one lands; the rules are SearchResultsPolicy.
    //
    // UI-thread only. One debounce timer, stopped on destruction; the service
    // and the timer reach this only through weak references.
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
        [[nodiscard]] winrt::Windows::Foundation::IInspectable PopularItems() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable LibraryItems() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable ResultItems() const;
        auto SuggestionsView() const { return m_suggestions; }
        auto RecentItemsView() const { return m_recent; }
        auto PopularItemsView() const { return m_popular; }
        auto LibraryItemsView() const { return m_library; }
        auto ResultItemsView() const { return m_results; }
        [[nodiscard]] bool ShowKindBadges() const noexcept;
        [[nodiscard]] winrt::hstring ResultCountText() const;
        [[nodiscard]] winrt::hstring FailureNote() const;
        [[nodiscard]] winrt::hstring StillAskingText() const;
        [[nodiscard]] bool StillAsking() const noexcept;
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
        [[nodiscard]] winrt::hstring TopMatchActionLabel() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopMatchRatingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopMatchYearVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopMatchInLibraryVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopMatchBlurbVisibility() const noexcept;

        [[nodiscard]] Microsoft::UI::Xaml::Visibility SuggestionsVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RecentVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PopularVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PopularGridVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PopularSkeletonVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FailureNoteVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility NoAddonsVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopMatchVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility TopMatchSkeletonVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility LibraryVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ResultsVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ResultsSkeletonVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility StillAskingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility FilteredEmptyVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility NothingFoundVisibility() const noexcept;
        // Whether any skeleton is drawn, for the page's pulse.
        [[nodiscard]] bool SkeletonsShown() const noexcept;

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
        // Opening a result keeps the query among the recent terms.
        void OpenDetail(winrt::Windows::Foundation::IInspectable const& item);
        void OpenTopMatch();
        // Home's primary action: a series needs an episode chosen, and the
        // title page is the picker; a film it can play goes to its sources.
        void PlayTopMatch();
        // A result card whose poster did not load: its title counts as artless.
        void ReportMissingArt(winrt::Windows::Foundation::IInspectable const& item);
        // Arriving with Home's text: searched at once, not recorded.
        void Arrive(winrt::hstring const& query);
        void Shown();
        // Keeps the query among the recent terms, which opening any of its
        // results does, from the poster menu too.
        void RecordQuery();

        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void SetQueryText(winrt::hstring const& value);
        void SearchNow();
        void Recompose();
        void RebuildHistoryLists();
        [[nodiscard]] std::optional<std::wstring> FilterType() const;
        void Raise(wchar_t const* name);
        void RaiseAll();

        std::shared_ptr<::HaloDesktop::Services::SearchService> m_search;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_debounceTimer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        ::HaloDesktop::Services::SearchChangedToken m_searchToken{};
        std::uint64_t m_accountVersion{};

        winrt::hstring m_query;
        // The query the service was last asked, before trimming.
        std::wstring m_activeQuery;
        std::int32_t m_filterIndex{};
        bool m_boxFocused{};
        // Keys (`type:id`) of results whose poster failed to load on screen.
        std::unordered_set<std::wstring> m_missingArt;

        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable>
            m_suggestions{ nullptr }, m_recent{ nullptr }, m_popular{ nullptr }, m_library{ nullptr }, m_results{ nullptr };
        // What each collection holds, so an answer that changes nothing leaves
        // the realised cards alone.
        std::vector<::HaloDesktop::Services::SearchTitle> m_popularShown, m_libraryShown, m_resultsShown;
        std::vector<std::wstring> m_recentTerms, m_suggestionTerms;

        std::optional<::HaloDesktop::Services::SearchTitle> m_topMatchTitle;
        winrt::HaloDesktop::MediaSummary m_topMatch{ nullptr };
        bool m_topMatchInLibrary{};
        winrt::hstring m_failureNote;
        winrt::hstring m_stillAsking;
        std::size_t m_resultCount{};
        bool m_active{};
        bool m_suggestionsShown{};
        bool m_recentShown{};
        bool m_popularSection{};
        bool m_popularSkeleton{};
        bool m_noAddons{};
        bool m_topMatchSkeleton{};
        bool m_resultsSkeleton{};
        bool m_stillAskingShown{};
        bool m_filteredEmpty{};
        bool m_nothingFound{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
