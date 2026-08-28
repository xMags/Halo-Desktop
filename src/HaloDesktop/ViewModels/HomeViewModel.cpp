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
#include <optional>
#include <utility>
#include <vector>
namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
}
namespace winrt::HaloDesktop::implementation
{
    HomeViewModel::HomeViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_layout(services.LayoutMetrics), m_catalog(services.Catalog), m_library(services.Library),
          m_navigation(services.Navigation),
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
    winrt::hstring HomeViewModel::HeroTitle() const { return m_hero ? m_hero.Title() : L""; }
    winrt::hstring HomeViewModel::HeroSynopsis() const { return m_hero ? m_hero.Description() : L""; }
    winrt::hstring HomeViewModel::HeroRating() const { return m_hero && !m_hero.Rating().empty() ? L"★ " + m_hero.Rating() : L""; }
    winrt::hstring HomeViewModel::HeroMeta() const { return m_hero ? m_hero.ReleaseInfo() : L""; }
    winrt::hstring HomeViewModel::HeroBackground() const { return m_hero ? (!m_hero.Background().empty() ? m_hero.Background() : m_hero.Poster()) : L""; }
    winrt::hstring HomeViewModel::HeroActionLabel() const { return m_hero && m_hero.Kind() == winrt::HaloDesktop::MediaKind::Series ? L"Choose episode" : L"Play"; }
    winrt::hstring HomeViewModel::HeroLibraryLabel() const { return m_heroLibraryBusy ? L"Saving…" : (m_heroInLibrary ? L"In library" : L"Add to library"); }
    bool HomeViewModel::HeroLibraryBusy() const noexcept { return m_heroLibraryBusy; }
    bool HomeViewModel::HeroLibraryEnabled() const noexcept { return m_hero && !m_heroLibraryBusy; }
    Microsoft::UI::Xaml::Visibility HomeViewModel::HeroVisibility() const noexcept { return m_hero ? Visible : Collapsed; }
    winrt::hstring HomeViewModel::HeroLibraryErrorText() const { return m_heroLibraryError; }
    Microsoft::UI::Xaml::Visibility HomeViewModel::HeroLibraryErrorVisibility() const noexcept { return m_heroLibraryError.empty() ? Collapsed : Visible; }
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
        return m_hero || m_shelves.Size() > 0 || m_continueItems.Size() > 0;
    }

    void HomeViewModel::EnsureLoaded()
    {
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
    void HomeViewModel::OpenHeroDetail() { OpenDetail(m_hero); }
    void HomeViewModel::OpenHeroSources()
    {
        if (!m_hero)
        {
            return;
        }
        if (m_hero.Kind() == winrt::HaloDesktop::MediaKind::Series)
        {
            OpenHeroDetail();
            return;
        }
        m_navigation->ShowSheet(
            ::HaloDesktop::Services::Page::Sources,
            winrt::make<winrt::HaloDesktop::implementation::SourcesNavParams>(
                m_hero.Type(),
                m_hero.Id(),
                m_hero.Id(),
                m_hero.Type() + L":" + m_hero.Id(),
                m_hero.Title(),
                m_hero.Title(),
                L"",
                m_hero.Poster()));
    }
    void HomeViewModel::ToggleHeroLibrary()
    {
        if (m_hero && !m_heroLibraryBusy)
        {
            static_cast<void>(ToggleHeroLibraryAsync());
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

    winrt::Windows::Foundation::IAsyncAction HomeViewModel::ToggleHeroLibraryAsync()
    {
        auto lifetime = get_strong();
        if (!m_hero || m_heroLibraryBusy)
        {
            co_return;
        }

        auto const uiContext = winrt::apartment_context{};
        auto const hero = m_hero;
        auto const nextMembership = !m_heroInLibrary;
        m_heroLibraryBusy = true;
        m_heroLibraryError.clear();
        for (auto const property : {
                 L"HeroLibraryLabel",
                 L"HeroLibraryEnabled",
                 L"HeroLibraryBusy",
                 L"HeroLibraryErrorText",
                 L"HeroLibraryErrorVisibility" })
        {
            Raise(property);
        }

        bool failed{};
        try
        {
            co_await m_library->SetMembershipAsync(
                hero.Type(),
                hero.Id(),
                hero.Title(),
                hero.Poster().empty()
                    ? std::nullopt
                    : std::optional<winrt::hstring>{ hero.Poster() },
                nextMembership);
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;

        m_heroLibraryBusy = false;
        if (!failed)
        {
            m_catalog->RebuildLibrary();
            m_sourceShelves = m_catalog->Shelves();
            m_appliedVersion = m_catalog->SnapshotVersion();
            ApplyContinue();
            Rebuild();
        }
        else if (m_hero && m_hero.Type() == hero.Type() && m_hero.Id() == hero.Id())
        {
            m_heroLibraryError = L"Library could not be updated. Try again.";
        }
        SynchronizeHeroLibraryState();
        RaiseState();
    }

    void HomeViewModel::Rebuild()
    {
        m_heroLibraryError.clear();
        m_hero = m_catalog->FeaturedForFilter(m_filterIndex);
        SynchronizeHeroLibraryState();
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

    void HomeViewModel::SynchronizeHeroLibraryState()
    {
        m_heroInLibrary = m_hero && m_library->Contains(m_hero.Type(), m_hero.Id());
    }

    void HomeViewModel::RaiseState()
    {
        for (auto const name : {
                 L"HeroTitle",
                 L"HeroSynopsis",
                 L"HeroRating",
                 L"HeroMeta",
                 L"HeroBackground",
                 L"HeroActionLabel",
                 L"HeroLibraryLabel",
                 L"HeroLibraryBusy",
                 L"HeroLibraryEnabled",
                 L"HeroVisibility",
                 L"HeroLibraryErrorText",
                 L"HeroLibraryErrorVisibility",
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
