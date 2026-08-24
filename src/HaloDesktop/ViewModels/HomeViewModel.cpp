#include "pch.h"
#include "ViewModels/HomeViewModel.h"
#if __has_include("HomeViewModel.g.cpp")
#include "HomeViewModel.g.cpp"
#endif
#include "Models/Models.h"
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
        : m_catalog(services.Catalog), m_navigation(services.Navigation),
          m_continueItems(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_shelves(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        static_cast<void>(LoadAsync());
    }
    winrt::hstring HomeViewModel::HeroTitle() const { return m_hero ? m_hero.Title() : L""; }
    winrt::hstring HomeViewModel::HeroSynopsis() const { return m_hero ? m_hero.Description() : L""; }
    winrt::hstring HomeViewModel::HeroRating() const { return m_hero && !m_hero.Rating().empty() ? L"★ " + m_hero.Rating() : L""; }
    winrt::hstring HomeViewModel::HeroMeta() const { return m_hero ? m_hero.ReleaseInfo() : L""; }
    winrt::hstring HomeViewModel::HeroBackground() const { return m_hero ? (!m_hero.Background().empty() ? m_hero.Background() : m_hero.Poster()) : L""; }
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
    void HomeViewModel::OpenDetail(winrt::Windows::Foundation::IInspectable const& item) { if (item) m_navigation->GoTo(::HaloDesktop::Services::Page::Detail, item); }
    void HomeViewModel::OpenHeroDetail() { OpenDetail(m_hero); }
    void HomeViewModel::OpenHeroSources() { if (m_hero) m_navigation->GoTo(::HaloDesktop::Services::Page::Sources, m_hero); }
    void HomeViewModel::OpenContinue(winrt::Windows::Foundation::IInspectable const& item) { if (item) m_navigation->GoTo(::HaloDesktop::Services::Page::Sources, item); }
    void HomeViewModel::OpenSearch(winrt::hstring const& query) { m_navigation->GoTo(::HaloDesktop::Services::Page::Search, winrt::box_value(query)); }
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
        for (auto const name : { L"HeroTitle", L"HeroSynopsis", L"HeroRating", L"HeroMeta", L"HeroBackground", L"ContinueItems", L"ContinueCountLabel", L"Shelves", L"ContentVisibility", L"LoadingVisibility", L"ErrorVisibility", L"EmptyVisibility" }) Raise(name);
    }
    void HomeViewModel::Raise(wchar_t const* name) { ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, name); }
}
