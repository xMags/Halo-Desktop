#include "pch.h"
#include "ViewModels/DetailViewModel.h"
#if __has_include("DetailEpisodeViewModel.g.cpp")
#include "DetailEpisodeViewModel.g.cpp"
#endif
#if __has_include("DetailViewModel.g.cpp")
#include "DetailViewModel.g.cpp"
#endif

#include "Services/NavigationService.h"
#include "ViewModels/ObservableHelper.h"

#include <utility>

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
}

namespace winrt::HaloDesktop::implementation
{
    DetailEpisodeViewModel::DetailEpisodeViewModel(winrt::HaloDesktop::Episode episode)
        : m_episode(std::move(episode))
    {
    }
    winrt::hstring DetailEpisodeViewModel::Tag() const { return m_episode.Tag(); }
    winrt::hstring DetailEpisodeViewModel::Title() const { return m_episode.Title(); }
    winrt::hstring DetailEpisodeViewModel::Blurb() const { return m_episode.Blurb(); }
    winrt::hstring DetailEpisodeViewModel::Runtime() const { return m_episode.Runtime(); }
    winrt::hstring DetailEpisodeViewModel::Aired() const { return m_episode.Aired(); }
    double DetailEpisodeViewModel::Progress() const noexcept { return m_episode.Progress(); }
    Microsoft::UI::Xaml::Visibility DetailEpisodeViewModel::SavedVisibility() const noexcept { return m_episode.Downloaded() ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DetailEpisodeViewModel::InProgressVisibility() const noexcept { return m_episode.Progress() > 0.0 && m_episode.Progress() < 1.0 ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility DetailEpisodeViewModel::IdleVisibility() const noexcept { return m_episode.Progress() > 0.0 && m_episode.Progress() < 1.0 ? Collapsed : Visible; }

    DetailViewModel::DetailViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_metadata(services.Metadata),
          m_navigation(services.Navigation),
          m_detail(services.Metadata->Detail()),
          m_episodes(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_facts(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>()),
          m_availability(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        for (auto const& fact : m_detail.Facts()) m_facts.Append(winrt::box_value(fact));
        for (auto const& item : m_detail.Availability()) m_availability.Append(winrt::box_value(item));
        RebuildEpisodes();
    }
    winrt::hstring DetailViewModel::Title() const { return m_detail.Title(); }
    winrt::hstring DetailViewModel::Kicker() const { return m_detail.Kicker(); }
    winrt::hstring DetailViewModel::MetaLine() const { return m_detail.MetaLine(); }
    winrt::hstring DetailViewModel::Synopsis() const { return m_detail.Synopsis(); }
    winrt::hstring DetailViewModel::SeasonMeta() const { return m_seasonIndex == 0 ? L"8 EPISODES · 8 WATCHED" : L"10 EPISODES · 4 DOWNLOADED"; }
    std::int32_t DetailViewModel::SeasonIndex() const noexcept { return m_seasonIndex; }
    winrt::Windows::Foundation::IInspectable DetailViewModel::Episodes() const { return m_episodes; }
    winrt::Windows::Foundation::IInspectable DetailViewModel::Facts() const { return m_facts; }
    winrt::Windows::Foundation::IInspectable DetailViewModel::Availability() const { return m_availability; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> DetailViewModel::EpisodesView() const { return m_episodes; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> DetailViewModel::FactsView() const { return m_facts; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> DetailViewModel::AvailabilityView() const { return m_availability; }
    void DetailViewModel::SelectSeason(std::int32_t index)
    {
        if (index < 0 || index > 1 || index == m_seasonIndex) return;
        m_seasonIndex = index;
        RebuildEpisodes();
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, L"SeasonIndex");
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, L"SeasonMeta");
    }
    void DetailViewModel::OpenSources() { m_navigation->GoTo(::HaloDesktop::Services::Page::Sources); }
    void DetailViewModel::OpenPlayer() { m_navigation->ShowOverlay(::HaloDesktop::Services::Page::Player); }
    void DetailViewModel::OpenDownloads() { m_navigation->GoTo(::HaloDesktop::Services::Page::Downloads); }
    winrt::event_token DetailViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void DetailViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    void DetailViewModel::RebuildEpisodes()
    {
        m_episodes.Clear();
        for (auto const& episode : m_metadata->Episodes(m_seasonIndex + 1))
            m_episodes.Append(winrt::make<DetailEpisodeViewModel>(episode));
    }
}
