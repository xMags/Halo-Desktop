#pragma once
#include "HistoryEntryViewModel.g.h"
#include "HistoryGroupViewModel.g.h"
#include "HistoryViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/HistoryRowsPolicy.h"
#include "Services/ServiceInterfaces.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace HaloDesktop::Services
{
    class LibraryService;
    class NavigationService;
    class TitleActions;
    class WatchStateService;
}

namespace winrt::HaloDesktop::implementation
{
    struct HistoryEntryViewModel : HistoryEntryViewModelT<HistoryEntryViewModel>
    {
        // Every string is worked out here, against the clock and calendar the
        // page was built with, so a card draws without asking again.
        HistoryEntryViewModel(
            ::HaloDesktop::Services::HistoryEntry entry,
            std::int64_t now,
            ::HaloDesktop::Services::LocalTimeOf const& localTime,
            bool firstInSection);

        [[nodiscard]] winrt::hstring Title() const;
        [[nodiscard]] winrt::hstring Tag() const;
        [[nodiscard]] winrt::hstring State() const;
        [[nodiscard]] winrt::hstring Time() const;
        [[nodiscard]] winrt::hstring FullTime() const;
        [[nodiscard]] winrt::hstring Poster() const;
        [[nodiscard]] winrt::hstring MetaId() const;
        [[nodiscard]] double ProgressWidth() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ProgressVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility WatchedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DividerVisibility() const noexcept;
        [[nodiscard]] winrt::hstring PlayGlyph() const;
        [[nodiscard]] winrt::hstring PlayLabel() const;
        [[nodiscard]] winrt::hstring OpenLabel() const;

        [[nodiscard]] ::HaloDesktop::Services::HistoryEntry const& Entry() const noexcept;

    private:
        ::HaloDesktop::Services::HistoryEntry m_entry;
        winrt::hstring m_title;
        winrt::hstring m_tag;
        winrt::hstring m_state;
        winrt::hstring m_time;
        winrt::hstring m_fullTime;
        double m_progress{};
        bool m_firstInSection{};
    };

    struct HistoryGroupViewModel : HistoryGroupViewModelT<HistoryGroupViewModel>
    {
        HistoryGroupViewModel(
            winrt::hstring label,
            winrt::hstring count,
            winrt::Windows::Foundation::Collections::IVector<winrt::Windows::Foundation::IInspectable> entries);

        [[nodiscard]] winrt::hstring Label() const;
        [[nodiscard]] winrt::hstring Count() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVector<winrt::Windows::Foundation::IInspectable> Entries() const;

    private:
        winrt::hstring m_label;
        winrt::hstring m_count;
        winrt::Windows::Foundation::Collections::IVector<winrt::Windows::Foundation::IInspectable> m_entries{ nullptr };
    };

    // The title page's details for the show a History row belongs to, when the
    // title page has them loaded: what "Mark N earlier episodes as watched" needs.
    struct HistoryCachedShow final
    {
        winrt::HaloDesktop::Episode Episode{ nullptr };
        std::vector<winrt::HaloDesktop::Episode> Episodes;
        winrt::hstring Name;
        winrt::hstring Poster;
    };

    // The History page's state, as the reference's `history/History.tsx` lays
    // it out: the search, the kind filter and the grouped rows, rebuilt from the
    // watch history the services already hold whenever the pages are told it
    // changed. The rules are HistoryRowsPolicy.
    //
    // UI-thread only. The services reach this only through weak references, and
    // so does every continuation after an await. An account change drops what
    // is shown and retires any load or removal still out for the old account.
    struct HistoryViewModel : HistoryViewModelT<HistoryViewModel>
    {
        explicit HistoryViewModel(::HaloDesktop::Services::AppServices const& services);
        ~HistoryViewModel();
        HistoryViewModel(HistoryViewModel const&) = delete;
        HistoryViewModel& operator=(HistoryViewModel const&) = delete;

        [[nodiscard]] winrt::hstring Query() const;
        void Query(winrt::hstring const& value);
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ClearQueryVisibility() const noexcept;
        [[nodiscard]] std::int32_t FilterIndex() const noexcept;
        [[nodiscard]] winrt::hstring AllTabText() const;
        [[nodiscard]] winrt::hstring MoviesTabText() const;
        [[nodiscard]] winrt::hstring SeriesTabText() const;
        [[nodiscard]] winrt::hstring InProgressTabText() const;
        [[nodiscard]] bool CanClear() const noexcept;
        [[nodiscard]] winrt::hstring Note() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility NoteVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ErrorVisibility() const noexcept;
        void SetFilter(std::int32_t index);
        void Retry();
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

        // Not projected: what the page binds and asks.
        [[nodiscard]] auto GroupsView() const { return m_groups; }
        // The page is on screen again: subscribes on first use, and works the
        // groups out afresh, so a page left open past midnight moves yesterday's
        // "Today" along.
        void Shown();
        void Hidden() noexcept;
        // A finished video opens its title page; any other resumes.
        void Open(winrt::HaloDesktop::HistoryEntryViewModel const& item) const;
        // Opens the sources for the video exactly as a Continue watching card
        // does: the title page with the sheet over it.
        void Play(winrt::HaloDesktop::HistoryEntryViewModel const& item) const;
        [[nodiscard]] std::optional<HistoryCachedShow> CachedShow(::HaloDesktop::Services::HistoryEntry const& entry) const;
        // Every live video in the history now, which is what Clear history asks
        // about and, once confirmed, forgets (whatever is still live of it).
        [[nodiscard]] std::vector<winrt::hstring> ClearSnapshot() const;
        // Both answer with what went wrong, for the page to say, or nothing when
        // the removal landed or no longer concerns the account signed in.
        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> RemoveVideoAsync(winrt::hstring videoId);
        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> ClearAsync(std::vector<winrt::hstring> videoIds);

    private:
        [[nodiscard]] concurrency::task<std::optional<winrt::hstring>> RemoveVideosAsync(std::vector<winrt::hstring> videoIds);
        winrt::fire_and_forget LoadAsync();
        void Subscribe();
        void AdoptHistoryChange();
        void ResetForAccount();
        void Rebuild();
        void RaiseState();
        void Raise(wchar_t const* name);

        std::shared_ptr<::HaloDesktop::Services::ICatalogService> m_catalog;
        std::shared_ptr<::HaloDesktop::Services::IMetadataService> m_metadata;
        std::shared_ptr<::HaloDesktop::Services::ISessionService> m_session;
        std::shared_ptr<::HaloDesktop::Services::LibraryService> m_library;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        std::shared_ptr<::HaloDesktop::Services::WatchStateService> m_watch;
        std::shared_ptr<::HaloDesktop::Services::TitleActions> m_actions;
        ::HaloDesktop::Services::CatalogChangedToken m_userStateToken{};
        ::HaloDesktop::Services::CatalogChangedToken m_homeToken{};
        ::HaloDesktop::Services::IdentityChangedToken m_identityToken{};
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_groups{ nullptr };
        // What m_groups was built from, and the local day it was built on, so a
        // notice that changed nothing on screen rebuilds no cards.
        std::vector<::HaloDesktop::Services::HistoryGroup> m_shownGroups;
        std::int64_t m_shownDay{ -1 };
        winrt::hstring m_query;
        std::int32_t m_filterIndex{};
        // What each tab would show under the search, for their labels.
        ::HaloDesktop::Services::HistoryViewCounts m_counts;
        ::HaloDesktop::Services::HistoryLoadState m_state{ ::HaloDesktop::Services::HistoryLoadState::Loading };
        bool m_hasAny{};
        bool m_groupsEmpty{ true };
        bool m_shown{};
        bool m_subscribed{};
        std::uint64_t m_loadVersion{};
        // The account this was last reset for, and the removals still out for it.
        std::uint64_t m_generation{};
        std::uint32_t m_removalsInFlight{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
