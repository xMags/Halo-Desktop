#include "pch.h"
#include "ViewModels/HomeViewModel.h"
#if __has_include("HomeViewModel.g.cpp")
#include "HomeViewModel.g.cpp"
#endif
#include "Models/Models.h"
#include "Services/NavigationService.h"
#include "Services/SampleData.h"
#include "ViewModels/ObservableHelper.h"
#include <utility>
#include <vector>

namespace winrt::HaloDesktop::implementation
{
    HomeViewModel::HomeViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_catalog(services.Catalog), m_navigation(services.Navigation), m_hero(services.Catalog->Hero()),
          m_continueItems(services.Catalog->ContinueWatching()), m_sourceShelves(services.Catalog->Shelves()),
          m_continueItemsView(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_shelves(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        for (auto const& item : m_continueItems) m_continueItemsView.Append(item);
        RebuildShelves();
    }
    winrt::hstring HomeViewModel::HeroTitle() const { return m_hero.Title(); }
    winrt::hstring HomeViewModel::HeroSynopsis() const { return ::HaloDesktop::Services::SampleData::Copy::HeroSynopsis; }
    winrt::hstring HomeViewModel::HeroRating() const { return ::HaloDesktop::Services::SampleData::Copy::HeroRating; }
    winrt::hstring HomeViewModel::HeroMeta() const { return ::HaloDesktop::Services::SampleData::Copy::HeroMeta; }
    winrt::hstring HomeViewModel::CatalogStats() const { return ::HaloDesktop::Services::SampleData::Copy::HomeCatalogStats; }
    winrt::Windows::Foundation::IInspectable HomeViewModel::ContinueItems() const { return m_continueItemsView; }
    winrt::Windows::Foundation::IInspectable HomeViewModel::Shelves() const { return m_shelves; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> HomeViewModel::ContinueItemsView() const { return m_continueItemsView; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> HomeViewModel::ShelvesView() const { return m_shelves; }
    std::int32_t HomeViewModel::FilterIndex() const noexcept { return m_filterIndex; }
    void HomeViewModel::SetFilter(std::int32_t index)
    {
        if (index < 0 || index > 2 || m_filterIndex == index) return;
        m_filterIndex = index;
        RebuildShelves();
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, L"FilterIndex");
    }
    void HomeViewModel::OpenDetail() { m_navigation->GoTo(::HaloDesktop::Services::Page::Detail); }
    void HomeViewModel::OpenPlayer() { m_navigation->ShowOverlay(::HaloDesktop::Services::Page::Player); }
    void HomeViewModel::OpenSearch(winrt::hstring const& query) { m_navigation->GoTo(::HaloDesktop::Services::Page::Search, winrt::box_value(query)); }
    winrt::event_token HomeViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void HomeViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    void HomeViewModel::RebuildShelves()
    {
        m_shelves.Clear();
        for (auto const& shelf : m_sourceShelves)
        {
            std::vector<winrt::HaloDesktop::MediaSummary> filtered;
            for (auto const& item : shelf.Items())
            {
                auto const include = m_filterIndex == 0
                    || (m_filterIndex == 1 && item.Kind() == winrt::HaloDesktop::MediaKind::Movie)
                    || (m_filterIndex == 2 && item.Kind() == winrt::HaloDesktop::MediaKind::Series);
                if (include) filtered.push_back(item);
            }
            auto items = winrt::single_threaded_vector<winrt::HaloDesktop::MediaSummary>(std::move(filtered)).GetView();
            m_shelves.Append(winrt::make<winrt::HaloDesktop::implementation::Shelf>(shelf.Title(), shelf.SourceLabel(), items));
        }
    }
}
