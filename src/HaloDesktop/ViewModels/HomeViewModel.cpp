#include "pch.h"
#include "ViewModels/HomeViewModel.h"
#if __has_include("HomeViewModel.g.cpp")
#include "HomeViewModel.g.cpp"
#endif
#include "Models/Models.h"
#include "Shell/LayoutMetricsService.h"
#include "Services/NavigationService.h"
#include "ViewModels/ObservableHelper.h"
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
        : m_layout(services.LayoutMetrics), m_catalog(services.Catalog), m_navigation(services.Navigation),
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
    winrt::hstring HomeViewModel::ContinueCountLabel() const { return winrt::to_hstring(m_continueItems.Size()) + L" IN PROGRESS"; }
    winrt::Windows::Foundation::IInspectable HomeViewModel::ContinueItems() const { return m_continueItems; }
    winrt::Windows::Foundation::IInspectable HomeViewModel::Shelves() const { return m_shelves; }
    std::int32_t HomeViewModel::FilterIndex() const noexcept { return m_filterIndex; }
    Microsoft::UI::Xaml::Visibility HomeViewModel::ContentVisibility() const noexcept { return !m_loading && !m_error && (m_hero || m_shelves.Size() || m_continueItems.Size()) ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility HomeViewModel::LoadingVisibility() const noexcept { return m_loading ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility HomeViewModel::ErrorVisibility() const noexcept { return m_error ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility HomeViewModel::EmptyVisibility() const noexcept { return !m_loading && !m_error && !m_hero && m_shelves.Size() == 0 ? Visible : Collapsed; }
    void HomeViewModel::SetFilter(std::int32_t index) { if (index >= 0 && index <= 2 && index != m_filterIndex) { m_filterIndex = index; Rebuild(); Raise(L"FilterIndex"); } }
    void HomeViewModel::Retry() { static_cast<void>(LoadAsync()); }
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
        m_navigation->GoTo(
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
    void HomeViewModel::OpenContinue(winrt::Windows::Foundation::IInspectable const& item) { if (item) m_navigation->GoTo(::HaloDesktop::Services::Page::Sources, item); }
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
        auto lifetime = get_strong(); auto const uiContext = winrt::apartment_context{}; auto const version = ++m_loadVersion;
        m_loading = true; m_error = false; RaiseState(); bool failed{};
        try { co_await m_catalog->LoadAsync(); } catch (...) { failed = true; }
        co_await uiContext; if (version != m_loadVersion) co_return;
        m_loading = false; m_error = failed;
        if (!failed)
        {
            m_hero = m_catalog->Hero(); m_sourceShelves = m_catalog->Shelves();
            m_continueItems.Clear(); for (auto const& item : m_catalog->ContinueWatching()) m_continueItems.Append(item);
            Rebuild();
        }
        RaiseState();
    }
    void HomeViewModel::Rebuild()
    {
        m_shelves.Clear(); if (!m_sourceShelves) return;
        for (auto const& shelf : m_sourceShelves)
        {
            std::vector<winrt::HaloDesktop::MediaSummary> filtered;
            for (auto const& item : shelf.Items()) if (m_filterIndex == 0 || (m_filterIndex == 1 && item.Kind() == winrt::HaloDesktop::MediaKind::Movie) || (m_filterIndex == 2 && item.Kind() == winrt::HaloDesktop::MediaKind::Series)) filtered.push_back(item);
            if (!filtered.empty()) m_shelves.Append(winrt::make<winrt::HaloDesktop::implementation::Shelf>(shelf.Title(), shelf.SourceLabel(), winrt::single_threaded_vector(std::move(filtered)).GetView()));
        }
        Raise(L"Shelves"); Raise(L"ContentVisibility"); Raise(L"EmptyVisibility");
    }
    void HomeViewModel::RaiseState()
    {
        for (auto const name : { L"HeroTitle", L"HeroSynopsis", L"HeroRating", L"HeroMeta", L"HeroBackground", L"HeroActionLabel", L"ContinueItems", L"ContinueCountLabel", L"Shelves", L"ContentVisibility", L"LoadingVisibility", L"ErrorVisibility", L"EmptyVisibility" }) Raise(name);
    }
    void HomeViewModel::Raise(wchar_t const* name) { ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, name); }
}
