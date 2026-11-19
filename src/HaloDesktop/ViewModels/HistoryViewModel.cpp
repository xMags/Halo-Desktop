#include "pch.h"
#include "ViewModels/HistoryViewModel.h"
#if __has_include("HistoryEntryViewModel.g.cpp")
#include "HistoryEntryViewModel.g.cpp"
#endif
#if __has_include("HistoryGroupViewModel.g.cpp")
#include "HistoryGroupViewModel.g.cpp"
#endif
#if __has_include("HistoryViewModel.g.cpp")
#include "HistoryViewModel.g.cpp"
#endif

#include "Models/Models.h"
#include "Services/LibraryService.h"
#include "Services/NavigationService.h"
#include "Services/TitleActions.h"
#include "Services/WatchStateService.h"
#include "ViewModels/HomeStatePolicy.h"
#include "ViewModels/ObservableHelper.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    // The row's progress bar.
    constexpr double ProgressBarWidth = 120.0;
    // The tabs, in the page's order: All, Movies, Series, In progress.
    constexpr std::int32_t LastFilterIndex = 3;
    // Segoe Fluent Icons.
    constexpr wchar_t ResumeGlyph[] = L"\xE768";
    constexpr wchar_t PlayAgainGlyph[] = L"\xE72C";
    constexpr wchar_t ServerFailedBody[] = L"Check the connection to your Halo server and try again.";

    std::int64_t NowMilliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    winrt::hstring TabText(wchar_t const* label, std::size_t count)
    {
        return winrt::hstring{ std::wstring{ label } + L" (" + std::to_wstring(count) + L")" };
    }

    std::int64_t LocalDayKey(std::int64_t now, ::HaloDesktop::Services::LocalTimeOf const& localTime)
    {
        auto const local = localTime(now);
        return static_cast<std::int64_t>(local.Year) * 10'000 + local.Month * 100 + local.Day;
    }
}

namespace winrt::HaloDesktop::implementation
{
    HistoryEntryViewModel::HistoryEntryViewModel(
        ::HaloDesktop::Services::HistoryEntry entry,
        std::int64_t now,
        ::HaloDesktop::Services::LocalTimeOf const& localTime,
        bool firstInSection)
        : m_entry(std::move(entry)), m_firstInSection(firstInSection)
    {
        m_title = winrt::hstring{ m_entry.Name };
        m_tag = winrt::hstring{ ::HaloDesktop::Services::VideoIdTag(m_entry.VideoId, m_entry.MetaId) };
        m_state = winrt::hstring{ ::HaloDesktop::Services::HistoryStateText(m_entry) };
        m_time = winrt::hstring{ ::HaloDesktop::Services::HistoryTime(m_entry.UpdatedAt, now, localTime) };
        m_fullTime = winrt::hstring{ ::HaloDesktop::Services::HistoryFullTime(m_entry.UpdatedAt, localTime) };
        m_progress = ::HaloDesktop::Services::HistoryProgress(m_entry);
    }

    winrt::hstring HistoryEntryViewModel::Title() const { return m_title; }
    winrt::hstring HistoryEntryViewModel::Tag() const { return m_tag; }
    winrt::hstring HistoryEntryViewModel::State() const { return m_state; }
    winrt::hstring HistoryEntryViewModel::Time() const { return m_time; }
    winrt::hstring HistoryEntryViewModel::FullTime() const { return m_fullTime; }
    winrt::hstring HistoryEntryViewModel::Poster() const { return winrt::hstring{ m_entry.Poster.value_or(std::wstring{}) }; }
    winrt::hstring HistoryEntryViewModel::MetaId() const { return winrt::hstring{ m_entry.MetaId }; }
    // Whole pixels of the bar, at least one, so a video just started shows it began.
    double HistoryEntryViewModel::ProgressWidth() const noexcept { return (std::max)(1.0, std::round(ProgressBarWidth * m_progress)); }
    // Any video of known length not yet finished, even one stopped in its
    // first second: the bar sits beside how much is left.
    Microsoft::UI::Xaml::Visibility HistoryEntryViewModel::ProgressVisibility() const noexcept
    {
        return !m_entry.Finished && m_entry.DurationSec > 0.0 ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility HistoryEntryViewModel::WatchedVisibility() const noexcept { return m_entry.Finished ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility HistoryEntryViewModel::DividerVisibility() const noexcept { return m_firstInSection ? Collapsed : Visible; }
    winrt::hstring HistoryEntryViewModel::PlayGlyph() const { return m_entry.Finished ? PlayAgainGlyph : ResumeGlyph; }
    winrt::hstring HistoryEntryViewModel::PlayLabel() const { return m_entry.Finished ? L"Play again" : L"Resume"; }
    winrt::hstring HistoryEntryViewModel::OpenLabel() const
    {
        return m_title + (m_entry.Finished ? L": open details" : L": resume");
    }
    ::HaloDesktop::Services::HistoryEntry const& HistoryEntryViewModel::Entry() const noexcept { return m_entry; }

    HistoryGroupViewModel::HistoryGroupViewModel(
        winrt::hstring label,
        winrt::hstring count,
        winrt::Windows::Foundation::Collections::IVector<winrt::Windows::Foundation::IInspectable> entries)
        : m_label(std::move(label)), m_count(std::move(count)), m_entries(std::move(entries))
    {
    }

    winrt::hstring HistoryGroupViewModel::Label() const { return m_label; }
    winrt::hstring HistoryGroupViewModel::Count() const { return m_count; }
    winrt::Windows::Foundation::Collections::IVector<winrt::Windows::Foundation::IInspectable> HistoryGroupViewModel::Entries() const { return m_entries; }

    HistoryViewModel::HistoryViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_catalog(services.Catalog),
          m_metadata(services.Metadata),
          m_session(services.Session),
          m_library(services.Library),
          m_navigation(services.Navigation),
          m_watch(services.WatchState),
          m_groups(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        if (!m_catalog || !m_metadata || !m_session || !m_library || !m_navigation || !m_watch)
        {
            throw std::invalid_argument{ "HistoryViewModel requires its services." };
        }
        m_actions = std::make_shared<::HaloDesktop::Services::TitleActions>(
            m_watch, m_library, m_catalog, m_metadata, m_session);
        m_generation = m_session->Generation();
    }

    HistoryViewModel::~HistoryViewModel()
    {
        if (m_userStateToken != 0) m_catalog->RemoveUserStateChangedHandler(m_userStateToken);
        if (m_homeToken != 0) m_catalog->RemoveHomeChangedHandler(m_homeToken);
        if (m_identityToken != 0) m_session->RemoveIdentityChangedHandler(m_identityToken);
    }

    winrt::hstring HistoryViewModel::Query() const { return m_query; }
    void HistoryViewModel::Query(winrt::hstring const& value)
    {
        if (m_query == value) return;
        m_query = value;
        Raise(L"Query");
        Raise(L"ClearQueryVisibility");
        Rebuild();
        RaiseState();
    }
    Microsoft::UI::Xaml::Visibility HistoryViewModel::ClearQueryVisibility() const noexcept { return m_query.empty() ? Collapsed : Visible; }
    std::int32_t HistoryViewModel::FilterIndex() const noexcept { return m_filterIndex; }
    winrt::hstring HistoryViewModel::AllTabText() const { return TabText(L"All", m_counts.All); }
    winrt::hstring HistoryViewModel::MoviesTabText() const { return TabText(L"Movies", m_counts.Movies); }
    winrt::hstring HistoryViewModel::SeriesTabText() const { return TabText(L"Series", m_counts.Series); }
    winrt::hstring HistoryViewModel::InProgressTabText() const { return TabText(L"In progress", m_counts.InProgress); }
    bool HistoryViewModel::CanClear() const noexcept
    {
        return m_state == ::HaloDesktop::Services::HistoryLoadState::Loaded && m_hasAny && m_removalsInFlight == 0;
    }
    winrt::hstring HistoryViewModel::Note() const
    {
        auto const note = ::HaloDesktop::Services::HistoryNote(
            m_state, m_hasAny, m_groupsEmpty, m_query, ::HaloDesktop::Services::HistoryViewFromIndex(m_filterIndex));
        return note ? winrt::hstring{ *note } : winrt::hstring{};
    }
    Microsoft::UI::Xaml::Visibility HistoryViewModel::NoteVisibility() const noexcept { return Note().empty() ? Collapsed : Visible; }
    Microsoft::UI::Xaml::Visibility HistoryViewModel::ErrorVisibility() const noexcept
    {
        return m_state == ::HaloDesktop::Services::HistoryLoadState::Failed ? Visible : Collapsed;
    }

    void HistoryViewModel::SetFilter(std::int32_t index)
    {
        if (index < 0 || index > LastFilterIndex || index == m_filterIndex) return;
        m_filterIndex = index;
        Raise(L"FilterIndex");
        Rebuild();
        RaiseState();
    }

    void HistoryViewModel::Retry()
    {
        if (m_session->Mode() == ::HaloDesktop::Services::SessionMode::SignedOut) return;
        m_state = ::HaloDesktop::Services::HistoryLoadState::Loading;
        Rebuild();
        RaiseState();
        LoadAsync();
    }

    winrt::event_token HistoryViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void HistoryViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }

    void HistoryViewModel::Shown()
    {
        m_shown = true;
        Subscribe();
        // An account that changed while the page was away, before this heard of it.
        if (m_generation != m_session->Generation())
        {
            ResetForAccount();
        }
        if (m_watch->HasLoaded())
        {
            m_state = ::HaloDesktop::Services::HistoryLoadState::Loaded;
            Rebuild();
            RaiseState();
            return;
        }
        m_state = ::HaloDesktop::Services::HistoryLoadState::Loading;
        Rebuild();
        RaiseState();
        if (m_session->Mode() != ::HaloDesktop::Services::SessionMode::SignedOut)
        {
            LoadAsync();
        }
    }

    void HistoryViewModel::Hidden() noexcept { m_shown = false; }

    void HistoryViewModel::Subscribe()
    {
        if (m_subscribed) return;
        m_subscribed = true;
        // A removal, a watched mark or a library change anywhere publishes here;
        // the history arriving with Home's first load does too.
        m_userStateToken = m_catalog->AddUserStateChangedHandler([weak = get_weak()]()
        {
            if (auto const self = weak.get()) self->AdoptHistoryChange();
        });
        m_homeToken = m_catalog->AddHomeChangedHandler([weak = get_weak()]()
        {
            if (auto const self = weak.get()) self->AdoptHistoryChange();
        });
        m_identityToken = m_session->AddIdentityChangedHandler([weak = get_weak()]()
        {
            auto const self = weak.get();
            if (!self) return;
            self->ResetForAccount();
            self->Rebuild();
            self->RaiseState();
            if (self->m_shown && self->m_session->Mode() != ::HaloDesktop::Services::SessionMode::SignedOut)
            {
                self->LoadAsync();
            }
        });
    }

    // The watch-state service is the truth: whatever it holds now is drawn.
    void HistoryViewModel::AdoptHistoryChange()
    {
        if (m_state != ::HaloDesktop::Services::HistoryLoadState::Loaded && m_watch->HasLoaded())
        {
            m_state = ::HaloDesktop::Services::HistoryLoadState::Loaded;
        }
        if (m_state != ::HaloDesktop::Services::HistoryLoadState::Loaded) return;
        Rebuild();
        RaiseState();
    }

    // Drops everything of the account that left: its rows, its load in flight
    // and its removals, which can no longer hold Clear history back.
    void HistoryViewModel::ResetForAccount()
    {
        ++m_loadVersion;
        m_generation = m_session->Generation();
        m_removalsInFlight = 0;
        m_state = ::HaloDesktop::Services::HistoryLoadState::Loading;
    }

    winrt::fire_and_forget HistoryViewModel::LoadAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        auto const weak = get_weak();
        auto const catalog = m_catalog;
        auto const version = ++m_loadVersion;
        auto failed = false;
        try
        {
            // Network free once loaded; otherwise the one shared load Home and
            // the shell also wait on.
            co_await catalog->LoadAsync();
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;
        auto const self = weak.get();
        if (!self || version != self->m_loadVersion) co_return;
        if (self->m_watch->HasLoaded())
        {
            self->m_state = ::HaloDesktop::Services::HistoryLoadState::Loaded;
        }
        else
        {
            self->m_state = failed
                ? ::HaloDesktop::Services::HistoryLoadState::Failed
                : ::HaloDesktop::Services::HistoryLoadState::Loading;
        }
        self->Rebuild();
        self->RaiseState();
    }

    void HistoryViewModel::Rebuild()
    {
        std::vector<::HaloDesktop::Services::HistoryGroup> groups;
        m_hasAny = false;
        ::HaloDesktop::Services::HistoryViewCounts counts;
        auto const now = NowMilliseconds();
        auto const localTime = ::HaloDesktop::Services::SystemLocalTime();
        if (m_state == ::HaloDesktop::Services::HistoryLoadState::Loaded)
        {
            auto const rows = m_watch->Rows();
            auto const library = m_library->Rows();
            m_hasAny = !rows.empty();
            // One build serves every tab: the counts are of what each would
            // show under the same search.
            auto matching = ::HaloDesktop::Services::SearchHistory(
                ::HaloDesktop::Services::BuildHistory(rows, library, ::HaloDesktop::ViewModels::HomeFilter::All),
                m_query);
            counts = ::HaloDesktop::Services::CountHistoryViews(matching);
            groups = ::HaloDesktop::Services::GroupHistory(
                ::HaloDesktop::Services::FilterHistoryView(
                    std::move(matching), ::HaloDesktop::Services::HistoryViewFromIndex(m_filterIndex)),
                now,
                localTime);
        }
        if (counts != m_counts)
        {
            m_counts = counts;
            for (auto const name : { L"AllTabText", L"MoviesTabText", L"SeriesTabText", L"InProgressTabText" }) Raise(name);
        }
        m_groupsEmpty = groups.empty();
        auto const day = LocalDayKey(now, localTime);
        if (groups == m_shownGroups && day == m_shownDay)
        {
            return;
        }

        std::vector<winrt::Windows::Foundation::IInspectable> items;
        items.reserve(groups.size());
        for (auto const& group : groups)
        {
            std::vector<winrt::Windows::Foundation::IInspectable> entries;
            entries.reserve(group.Entries.size());
            for (auto const& entry : group.Entries)
            {
                entries.push_back(winrt::make<HistoryEntryViewModel>(entry, now, localTime, entries.empty()));
            }
            items.push_back(winrt::make<HistoryGroupViewModel>(
                winrt::hstring{ group.Label },
                winrt::hstring{ ::HaloDesktop::Services::HistoryTitleCount(group.Entries.size()) },
                winrt::single_threaded_vector(std::move(entries))));
        }
        m_groups.ReplaceAll(items);
        m_shownGroups = std::move(groups);
        m_shownDay = day;
    }

    void HistoryViewModel::Open(winrt::HaloDesktop::HistoryEntryViewModel const& item) const
    {
        if (!item) return;
        auto const& entry = winrt::get_self<HistoryEntryViewModel>(item)->Entry();
        if (!entry.Finished)
        {
            Play(item);
            return;
        }
        m_navigation->GoTo(
            ::HaloDesktop::Services::Page::Detail,
            winrt::make<winrt::HaloDesktop::implementation::DetailNavParams>(
                winrt::hstring{ entry.Type },
                winrt::hstring{ entry.MetaId },
                winrt::hstring{ entry.Name },
                winrt::hstring{ entry.Poster.value_or(std::wstring{}) }));
    }

    void HistoryViewModel::Play(winrt::HaloDesktop::HistoryEntryViewModel const& item) const
    {
        if (!item) return;
        auto const& entry = winrt::get_self<HistoryEntryViewModel>(item)->Entry();
        // The item a continue card carries, so the sheet and the title page
        // behind it open exactly as they do from Continue watching.
        auto const tag = winrt::hstring{ ::HaloDesktop::Services::VideoIdTag(entry.VideoId, entry.MetaId) };
        auto const continueItem = winrt::make<winrt::HaloDesktop::implementation::ContinueItem>(
            winrt::hstring{ entry.Name },
            tag,
            tag,
            winrt::hstring{},
            ::HaloDesktop::Services::HistoryProgress(entry),
            winrt::hstring{ entry.Type },
            winrt::hstring{ entry.MetaId },
            winrt::hstring{ entry.VideoId },
            winrt::hstring{ entry.ItemId },
            winrt::hstring{ entry.Poster.value_or(std::wstring{}) });
        ::HaloDesktop::Services::OpenContinueItem(*m_navigation, continueItem);
    }

    std::optional<HistoryCachedShow> HistoryViewModel::CachedShow(::HaloDesktop::Services::HistoryEntry const& entry) const
    {
        auto const detail = m_metadata->Detail();
        if (!detail || detail.Type() != entry.Type || detail.Id() != entry.MetaId)
        {
            return std::nullopt;
        }
        HistoryCachedShow show;
        auto const seasons = detail.Seasons();
        if (seasons)
        {
            for (auto const season : seasons)
            {
                for (auto const& episode : m_metadata->Episodes(season))
                {
                    if (!show.Episode && episode.VideoId() == entry.VideoId)
                    {
                        show.Episode = episode;
                    }
                    show.Episodes.push_back(episode);
                }
            }
        }
        if (!show.Episode)
        {
            return std::nullopt;
        }
        show.Name = detail.Title();
        show.Poster = detail.Poster();
        return show;
    }

    std::vector<winrt::hstring> HistoryViewModel::ClearSnapshot() const
    {
        std::vector<winrt::hstring> videoIds;
        if (m_state != ::HaloDesktop::Services::HistoryLoadState::Loaded) return videoIds;
        for (auto const& videoId : ::HaloDesktop::Services::ClearHistoryVideoIds(m_watch->Rows()))
        {
            videoIds.emplace_back(videoId);
        }
        return videoIds;
    }

    concurrency::task<std::optional<winrt::hstring>> HistoryViewModel::RemoveVideoAsync(winrt::hstring videoId)
    {
        std::vector<winrt::hstring> videoIds;
        if (!videoId.empty()) videoIds.push_back(std::move(videoId));
        co_return co_await RemoveVideosAsync(std::move(videoIds));
    }

    concurrency::task<std::optional<winrt::hstring>> HistoryViewModel::ClearAsync(std::vector<winrt::hstring> videoIds)
    {
        // The button is disabled while one is out; a second press that slipped
        // through changes nothing.
        if (m_removalsInFlight != 0) co_return std::nullopt;
        co_return co_await RemoveVideosAsync(std::move(videoIds));
    }

    concurrency::task<std::optional<winrt::hstring>> HistoryViewModel::RemoveVideosAsync(std::vector<winrt::hstring> videoIds)
    {
        if (videoIds.empty()) co_return std::nullopt;
        auto const uiContext = winrt::apartment_context{};
        auto const weak = get_weak();
        auto const actions = m_actions;
        auto const session = m_session;
        auto const generation = session->Generation();
        ++m_removalsInFlight;
        RaiseState();

        std::optional<winrt::hstring> failure;
        try
        {
            co_await actions->RemoveVideosFromHistoryAsync(std::move(videoIds));
        }
        catch (::HaloDesktop::Services::HistoryRemovalUnsupported const& error)
        {
            failure = winrt::to_hstring(error.what());
        }
        catch (...)
        {
            failure = winrt::hstring{ ServerFailedBody };
        }
        co_await uiContext;
        // A removal that outlived its account says nothing about the one now.
        if (session->Generation() != generation) co_return std::nullopt;
        if (auto const self = weak.get())
        {
            if (self->m_removalsInFlight > 0) --self->m_removalsInFlight;
            self->RaiseState();
        }
        co_return failure;
    }

    void HistoryViewModel::RaiseState()
    {
        for (auto const name : { L"CanClear", L"Note", L"NoteVisibility", L"ErrorVisibility" }) Raise(name);
    }

    void HistoryViewModel::Raise(wchar_t const* name)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, name);
    }
}
