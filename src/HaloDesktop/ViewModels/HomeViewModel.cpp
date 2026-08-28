#include "pch.h"
#include "ViewModels/HomeViewModel.h"
#if __has_include("HomeViewModel.g.cpp")
#include "HomeViewModel.g.cpp"
#endif
#include "Models/Models.h"
#include "Shell/LayoutMetricsService.h"
#include "Services/LibraryService.h"
#include "Services/NavigationService.h"
#include "ViewModels/HomeStatePolicy.h"
#include "ViewModels/ObservableHelper.h"
#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>
namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
    auto constexpr FeaturedCardCount = 5u;
}
namespace winrt::HaloDesktop::implementation
{
    HomeViewModel::HomeViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_layout(services.LayoutMetrics), m_catalog(services.Catalog), m_library(services.Library),
          m_navigation(services.Navigation),
          m_featured(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_continueItems(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_shelves(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        if (!m_layout) return;
        // Raw capture rather than a weak reference: get_weak has no controlling
        // reference to hand out this early in construction. It is sound because
        // the destructor below always removes the handler, and the service only
        // ever calls back synchronously on the same thread that owns this.
        m_metricsToken = m_layout->AddChangedHandler([this]() { RaiseLayoutMetrics(); });
    }
    HomeViewModel::~HomeViewModel()
    {
        if (m_layout && m_metricsToken != 0) m_layout->RemoveChangedHandler(m_metricsToken);
        if (m_featuredTimer)
        {
            m_featuredTimer.Stop();
            m_featuredTimer.Tick(m_featuredTickToken);
        }
    }
    // Hero art scales with the page because it is a backdrop, and its title with
    // it because that is a display element sized to the art. Body and label text
    // stays put: that is the user's DPI and text-size settings to decide.
    double HomeViewModel::HeroHeight() const noexcept { return m_layout ? m_layout->Current().HeroHeight : 304.0; }
    double HomeViewModel::HeroTitleSize() const noexcept { return m_layout ? m_layout->Current().HeroTitleSize : 36.0; }
    Microsoft::UI::Xaml::Thickness HomeViewModel::ContentPadding() const noexcept
    {
        auto const gutter = m_layout ? m_layout->Current().Gutter : 24.0;
        return Microsoft::UI::Xaml::Thickness{ gutter, 0.0, gutter, 0.0 };
    }
    winrt::Windows::Foundation::IInspectable HomeViewModel::FeaturedItems() const { return m_featured; }
    std::int32_t HomeViewModel::FeaturedCount() const noexcept { return static_cast<std::int32_t>(m_featured.Size()); }
    std::int32_t HomeViewModel::FeaturedIndex() const noexcept { return m_featuredIndex; }
    void HomeViewModel::FeaturedIndex(std::int32_t value)
    {
        // FlipView reports -1 while its source is being replaced; that is the
        // reset-to-first-card signal, not a real selection.
        if (value < 0) { value = 0; }
        auto const count = m_featured.Size();
        if (count == 0 || value >= static_cast<std::int32_t>(count)) { return; }
        if (value == m_featuredIndex) { return; }
        m_featuredIndex = value;
        Raise(L"FeaturedIndex");
        // A manual swipe or pip click restarts the countdown, so the strip sits
        // still for a full interval before moving on again.
        RestartFeaturedTimer();
    }
    Microsoft::UI::Xaml::Visibility HomeViewModel::FeaturedVisibility() const noexcept
    {
        return m_featured.Size() > 0 ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility HomeViewModel::RefreshErrorVisibility() const noexcept { return m_refreshError && HasUsableContent() ? Visible : Collapsed; }
    winrt::hstring HomeViewModel::ContinueCountLabel() const { return winrt::to_hstring(m_continueItems.Size()) + L" IN PROGRESS"; }
    winrt::Windows::Foundation::IInspectable HomeViewModel::ContinueItems() const { return m_continueItems; }
    winrt::Windows::Foundation::IInspectable HomeViewModel::Shelves() const { return m_shelves; }
    std::int32_t HomeViewModel::FilterIndex() const noexcept { return m_filterIndex; }
    Microsoft::UI::Xaml::Visibility HomeViewModel::ContentVisibility() const noexcept
    {
        return ::HaloDesktop::ViewModels::ResolveHomeVisibility(
            m_loading, m_error, HasUsableContent()).ShowContent ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility HomeViewModel::LoadingVisibility() const noexcept
    {
        return ::HaloDesktop::ViewModels::ResolveHomeVisibility(
            m_loading, m_error, HasUsableContent()).ShowLoading ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility HomeViewModel::ErrorVisibility() const noexcept
    {
        return ::HaloDesktop::ViewModels::ResolveHomeVisibility(
            m_loading, m_error, HasUsableContent()).ShowError ? Visible : Collapsed;
    }
    Microsoft::UI::Xaml::Visibility HomeViewModel::EmptyVisibility() const noexcept
    {
        return ::HaloDesktop::ViewModels::ResolveHomeVisibility(
            m_loading, m_error, HasUsableContent()).ShowEmpty ? Visible : Collapsed;
    }
    void HomeViewModel::SetFilter(std::int32_t index) { if (index >= 0 && index <= 2 && index != m_filterIndex) { m_filterIndex = index; Rebuild(); Raise(L"FilterIndex"); } }
    void HomeViewModel::Retry() { static_cast<void>(LoadAsync()); }

    bool HomeViewModel::HasUsableContent() const noexcept
    {
        return m_featured.Size() > 0 || m_shelves.Size() > 0 || m_continueItems.Size() > 0;
    }

    void HomeViewModel::EnsureLoaded()
    {
        m_active = true;
        if (!m_catalog->HasLoaded())
        {
            static_cast<void>(LoadAsync());
            return;
        }
        if (m_appliedVersion != m_catalog->SnapshotVersion())
        {
            // Either nothing has been shown yet, or something else reloaded the
            // catalogs while Home was away and this is now the older snapshot.
            AdoptSnapshot();
        }
        m_catalog->RefreshContinue();
        ApplyContinue();
        if (m_catalog->CatalogsDirty())
        {
            static_cast<void>(LoadAsync());
        }
        RestartFeaturedTimer();
    }

    void HomeViewModel::AdoptSnapshot()
    {
        // The catalogs finished loading for somebody else, so take what they hold
        // rather than fetching them again. Reached when this view model is built
        // after the shell has already warmed the catalog.
        m_sourceShelves = m_catalog->Shelves();
        m_loading = false;
        m_error = false;
        m_appliedVersion = m_catalog->SnapshotVersion();
        Rebuild();
        RaiseState();
    }

    void HomeViewModel::ApplyContinue()
    {
        // Replaced in one go. Clear followed by a run of appends raises a
        // notification per item, and the list on the other end tears down and
        // rebuilds its realised containers for every one of them.
        std::vector<winrt::Windows::Foundation::IInspectable> continued;
        for (auto const& item : m_catalog->ContinueWatching()) continued.push_back(item);
        m_continueItems.ReplaceAll(continued);
        for (auto const name : { L"ContinueItems", L"ContinueCountLabel", L"ContentVisibility", L"EmptyVisibility" }) Raise(name);
    }
    void HomeViewModel::OpenDetail(winrt::Windows::Foundation::IInspectable const& item) { if(item){auto media=item.as<winrt::HaloDesktop::MediaSummary>();m_navigation->GoTo(::HaloDesktop::Services::Page::Detail,winrt::make<winrt::HaloDesktop::implementation::DetailNavParams>(media.Type(),media.Id(),media.Title(),media.Poster()));} }
    void HomeViewModel::OpenFeaturedDetail(winrt::Windows::Foundation::IInspectable const& item)
    {
        if (auto const featured = item.try_as<winrt::HaloDesktop::FeaturedItem>())
        {
            OpenDetail(featured.Media());
        }
    }
    void HomeViewModel::OpenFeaturedSources(winrt::Windows::Foundation::IInspectable const& item)
    {
        auto const featured = item.try_as<winrt::HaloDesktop::FeaturedItem>();
        if (!featured)
        {
            return;
        }
        auto const media = featured.Media();
        if (media.Kind() == winrt::HaloDesktop::MediaKind::Series)
        {
            OpenFeaturedDetail(featured);
            return;
        }
        m_navigation->ShowSheet(
            ::HaloDesktop::Services::Page::Sources,
            winrt::make<winrt::HaloDesktop::implementation::SourcesNavParams>(
                media.Type(),
                media.Id(),
                media.Id(),
                media.Type() + L":" + media.Id(),
                media.Title(),
                media.Title(),
                L"",
                media.Poster()));
    }
    void HomeViewModel::ToggleFeaturedLibrary(winrt::Windows::Foundation::IInspectable const& item)
    {
        if (auto const featured = item.try_as<winrt::HaloDesktop::FeaturedItem>())
        {
            if (!featured.LibraryBusy())
            {
                static_cast<void>(ToggleFeaturedLibraryAsync(featured));
            }
        }
    }
    void HomeViewModel::OpenContinue(winrt::Windows::Foundation::IInspectable const& item)
    {
        ::HaloDesktop::Services::OpenContinueItem(
            *m_navigation,
            item.try_as<winrt::HaloDesktop::ContinueItem>());
    }
    void HomeViewModel::OpenSearch(winrt::hstring const& query) { m_navigation->GoTo(::HaloDesktop::Services::Page::Search, winrt::box_value(query)); }
    void HomeViewModel::OpenCatalog(winrt::Windows::Foundation::IInspectable const& shelf)
    {
        if (auto const snapshot = shelf.try_as<winrt::HaloDesktop::Shelf>())
        {
            m_navigation->GoTo(::HaloDesktop::Services::Page::Catalog, snapshot);
        }
    }
    // The shelf grid speaks MediaSummary, so the in-progress rows are projected
    // onto it. Time left and progress are dropped: the grid has nowhere to put
    // them, and the poster plus episode tag is what identifies a title there.
    void HomeViewModel::OpenContinueCatalog()
    {
        std::vector<winrt::HaloDesktop::MediaSummary> items;
        items.reserve(m_continueItems.Size());
        for (auto const& entry : m_continueItems)
        {
            auto const item = entry.try_as<winrt::HaloDesktop::ContinueItem>();
            if (!item || item.MetaId().empty()) continue;
            items.push_back(winrt::make<winrt::HaloDesktop::implementation::MediaSummary>(
                item.MetaId(),
                item.Name(),
                item.Sub(),
                item.Type() == L"series" ? winrt::HaloDesktop::MediaKind::Series : winrt::HaloDesktop::MediaKind::Movie,
                item.Type(),
                item.Poster()));
        }

        m_navigation->GoTo(
            ::HaloDesktop::Services::Page::Catalog,
            winrt::make<winrt::HaloDesktop::implementation::Shelf>(
                L"Continue watching",
                ContinueCountLabel(),
                winrt::single_threaded_vector(std::move(items)).GetView()));
    }
    void HomeViewModel::RaiseLayoutMetrics()
    {
        for (auto const* property : { L"HeroHeight", L"HeroTitleSize", L"ContentPadding" }) Raise(property);
        // The carousel re-renders at the new step, so every realised card gets
        // the new title size too; the template cannot see this view model.
        auto const titleSize = m_layout ? m_layout->Current().HeroTitleSize : 36.0;
        for (auto const& entry : m_featured)
        {
            if (auto const item = entry.try_as<winrt::HaloDesktop::FeaturedItem>())
            {
                winrt::get_self<winrt::HaloDesktop::implementation::FeaturedItem>(item)->SetTitleSize(titleSize);
            }
        }
    }
    winrt::event_token HomeViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void HomeViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    winrt::Windows::Foundation::IAsyncAction HomeViewModel::LoadAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const version = ++m_loadVersion;
        m_loading = !HasUsableContent();
        m_error = false;
        m_refreshError = false;
        RaiseState();

        bool failed{};
        try
        {
            if (m_catalog->HasLoaded())
            {
                co_await m_catalog->RefreshCatalogsIfDirtyAsync();
            }
            else
            {
                co_await m_catalog->LoadAsync();
            }
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;
        if (version != m_loadVersion)
        {
            co_return;
        }

        m_loading = false;
        if (!failed)
        {
            m_sourceShelves = m_catalog->Shelves();
            m_appliedVersion = m_catalog->SnapshotVersion();
            ApplyContinue();
            Rebuild();
        }
        else if (HasUsableContent())
        {
            m_refreshError = true;
        }
        else
        {
            m_error = true;
        }
        RaiseState();
    }

    winrt::Windows::Foundation::IAsyncAction HomeViewModel::ToggleFeaturedLibraryAsync(winrt::HaloDesktop::FeaturedItem item)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const media = item.Media();
        auto const nextMembership = !item.InLibrary();
        {
            // Scoped, not held across the await: the get_self pointer is raw and
            // the object is alive only because the projected handle keeps it so.
            auto const impl = winrt::get_self<winrt::HaloDesktop::implementation::FeaturedItem>(item);
            impl->SetLibraryBusy(true);
            impl->SetLibraryError({});
        }

        bool failed{};
        try
        {
            co_await m_library->SetMembershipAsync(
                media.Type(),
                media.Id(),
                media.Title(),
                media.Poster().empty()
                    ? std::nullopt
                    : std::optional<winrt::hstring>{ media.Poster() },
                nextMembership);
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;

        auto const impl = winrt::get_self<winrt::HaloDesktop::implementation::FeaturedItem>(item);
        impl->SetLibraryBusy(false);
        if (!failed)
        {
            impl->SetInLibrary(nextMembership);
            m_catalog->RebuildLibrary();
            m_sourceShelves = m_catalog->Shelves();
            m_appliedVersion = m_catalog->SnapshotVersion();
            ApplyContinue();
            Rebuild();
        }
        else
        {
            impl->SetLibraryError(L"Library could not be updated. Try again.");
        }
        RaiseState();
    }

    void HomeViewModel::Rebuild()
    {
        RebuildFeatured();
        if (!m_sourceShelves)
        {
            m_shelves.Clear();
            RaiseState();
            return;
        }

        auto const filter = ::HaloDesktop::ViewModels::HomeFilterFromIndex(m_filterIndex);
        std::vector<winrt::Windows::Foundation::IInspectable> rebuilt;
        for (auto const& shelf : m_sourceShelves)
        {
            std::vector<winrt::HaloDesktop::MediaSummary> filtered;
            for (auto const& item : shelf.Items())
            {
                auto const kind = item.Kind() == winrt::HaloDesktop::MediaKind::Series
                    ? ::HaloDesktop::ViewModels::HomeMediaKind::Series
                    : ::HaloDesktop::ViewModels::HomeMediaKind::Movie;
                if (::HaloDesktop::ViewModels::MatchesHomeFilter(filter, kind))
                {
                    filtered.push_back(item);
                }
            }
            if (!filtered.empty()) rebuilt.push_back(winrt::make<winrt::HaloDesktop::implementation::Shelf>(shelf.Title(), shelf.SourceLabel(), winrt::single_threaded_vector(std::move(filtered)).GetView()));
        }
        // One replacement rather than a clear and a run of appends: switching the
        // filter used to make the repeater rebuild a shelf at a time.
        m_shelves.ReplaceAll(rebuilt);
        RaiseState();
    }

    void HomeViewModel::RebuildFeatured()
    {
        auto const summaries = m_catalog->FeaturedSetForFilter(m_filterIndex, FeaturedCardCount);
        auto const titleSize = m_layout ? m_layout->Current().HeroTitleSize : 36.0;
        std::vector<winrt::Windows::Foundation::IInspectable> rebuilt;
        rebuilt.reserve(summaries.Size());
        for (auto const& summary : summaries)
        {
            rebuilt.push_back(winrt::make<winrt::HaloDesktop::implementation::FeaturedItem>(
                summary, m_library->Contains(summary.Type(), summary.Id()), titleSize));
        }
        // Membership changes rebuild everything, but the strip itself is usually
        // unchanged. Replacing it then would restart the carousel under the
        // pointer, so an identical run of titles keeps its cards and its place.
        bool unchanged = m_featured.Size() == rebuilt.size();
        if (unchanged)
        {
            for (std::uint32_t index = 0; index < rebuilt.size(); ++index)
            {
                auto const current = m_featured.GetAt(index).try_as<winrt::HaloDesktop::FeaturedItem>();
                auto const next = rebuilt[index].try_as<winrt::HaloDesktop::FeaturedItem>();
                if (!current || !next
                    || current.Media().Type() != next.Media().Type()
                    || current.Media().Id() != next.Media().Id())
                {
                    unchanged = false;
                    break;
                }
            }
        }
        if (!unchanged)
        {
            m_featured.ReplaceAll(rebuilt);
            m_featuredIndex = 0;
            Raise(L"FeaturedIndex");
        }
        else
        {
            // New cards carried the fresh membership; the kept ones may hold a
            // flag set before a save went through, so re-derive it for all.
            SynchronizeFeaturedLibraryState();
        }
        RestartFeaturedTimer();
    }

    void HomeViewModel::SynchronizeFeaturedLibraryState()
    {
        for (auto const& entry : m_featured)
        {
            auto const item = entry.try_as<winrt::HaloDesktop::FeaturedItem>();
            if (!item)
            {
                continue;
            }
            winrt::get_self<winrt::HaloDesktop::implementation::FeaturedItem>(item)
                ->SetInLibrary(m_library->Contains(item.Media().Type(), item.Media().Id()));
        }
    }

    void HomeViewModel::AdvanceFeatured()
    {
        auto const count = m_featured.Size();
        if (count < 2)
        {
            return;
        }
        FeaturedIndex((m_featuredIndex + 1) % static_cast<std::int32_t>(count));
    }

    void HomeViewModel::RestartFeaturedTimer()
    {
        if (m_featuredTimer)
        {
            m_featuredTimer.Stop();
        }
        if (!(m_active && !m_featuredPaused && m_featured.Size() >= 2))
        {
            return;
        }
        if (!m_featuredTimer)
        {
            m_featuredTimer = winrt::Microsoft::UI::Xaml::DispatcherTimer{};
            m_featuredTimer.Interval(std::chrono::seconds{ 7 });
            // Weak capture: the tick is queued through the dispatcher, so it can
            // outlive this view model unless Deactivate or the destructor stops it.
            m_featuredTickToken = m_featuredTimer.Tick([weak = get_weak()](
                [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&)
            {
                if (auto strong = weak.get())
                {
                    strong->AdvanceFeatured();
                }
            });
        }
        m_featuredTimer.Start();
    }

    void HomeViewModel::StopFeaturedTimer()
    {
        if (m_featuredTimer)
        {
            m_featuredTimer.Stop();
        }
    }

    // The strip stops advancing while a pointer rests on it, and stops entirely
    // once Home is off screen: a timer ticking behind another page would animate
    // a carousel nobody is looking at.
    void HomeViewModel::PauseFeatured()
    {
        if (m_featuredPaused) { return; }
        m_featuredPaused = true;
        StopFeaturedTimer();
    }
    void HomeViewModel::ResumeFeatured()
    {
        if (!m_featuredPaused) { return; }
        m_featuredPaused = false;
        RestartFeaturedTimer();
    }
    void HomeViewModel::Deactivate()
    {
        m_active = false;
        StopFeaturedTimer();
    }

    void HomeViewModel::RaiseState()
    {
        for (auto const name : {
                 L"FeaturedItems",
                 L"FeaturedCount",
                 L"FeaturedIndex",
                 L"FeaturedVisibility",
                 L"RefreshErrorVisibility",
                 L"ContinueItems",
                 L"ContinueCountLabel",
                 L"Shelves",
                 L"ContentVisibility",
                 L"LoadingVisibility",
                 L"ErrorVisibility",
                 L"EmptyVisibility" })
        {
            Raise(name);
        }
    }
    void HomeViewModel::Raise(wchar_t const* name) { ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, name); }
}
