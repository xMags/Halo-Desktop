#pragma once
#include "DetailEpisodeViewModel.g.h"
#include "DetailViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>
namespace winrt::HaloDesktop::implementation
{
    struct DetailEpisodeViewModel:DetailEpisodeViewModelT<DetailEpisodeViewModel>
    {explicit DetailEpisodeViewModel(winrt::HaloDesktop::Episode episode);winrt::hstring Tag()const;winrt::hstring Title()const;winrt::hstring Blurb()const;winrt::hstring Runtime()const;winrt::hstring Aired()const;winrt::hstring VideoId()const;double Progress()const noexcept;Microsoft::UI::Xaml::Visibility SavedVisibility()const noexcept;Microsoft::UI::Xaml::Visibility WatchedVisibility()const noexcept;Microsoft::UI::Xaml::Visibility InProgressVisibility()const noexcept;Microsoft::UI::Xaml::Visibility IdleVisibility()const noexcept;winrt::HaloDesktop::Episode Episode()const;private:winrt::HaloDesktop::Episode m_episode{nullptr};};
    struct DetailViewModel:DetailViewModelT<DetailViewModel>
    {
        explicit DetailViewModel(::HaloDesktop::Services::AppServices const& services);
        winrt::hstring Title()const;winrt::hstring Kicker()const;winrt::hstring MetaLine()const;winrt::hstring Synopsis()const;winrt::hstring SeasonMeta()const;winrt::hstring LibraryLabel()const;std::int32_t SeasonIndex()const noexcept;
        winrt::Windows::Foundation::IInspectable Episodes()const;winrt::Windows::Foundation::IInspectable Facts()const;winrt::Windows::Foundation::IInspectable Availability()const;winrt::Windows::Foundation::IInspectable Seasons()const;
        auto EpisodesView()const{return m_episodes;}auto FactsView()const{return m_facts;}auto AvailabilityView()const{return m_availability;}auto SeasonsView()const{return m_seasons;}
        Microsoft::UI::Xaml::Visibility ContentVisibility()const noexcept;Microsoft::UI::Xaml::Visibility LoadingVisibility()const noexcept;Microsoft::UI::Xaml::Visibility ErrorVisibility()const noexcept;
        void Load(winrt::Windows::Foundation::IInspectable const& parameter);void SelectSeason(std::int32_t index);void Retry();void ToggleLibrary();void OpenSources(winrt::Windows::Foundation::IInspectable const& episode);void BrowseSources();void OpenDownloads();
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const&);void PropertyChanged(winrt::event_token const&)noexcept;
    private:
        winrt::Windows::Foundation::IAsyncAction LoadAsync();winrt::Windows::Foundation::IAsyncAction ToggleLibraryAsync();void RebuildEpisodes();void RaiseState();void Raise(wchar_t const*);
        std::shared_ptr<::HaloDesktop::Services::IMetadataService>m_metadata;std::shared_ptr<::HaloDesktop::Services::LibraryService>m_library;std::shared_ptr<::HaloDesktop::Services::NavigationService>m_navigation;
        winrt::HaloDesktop::DetailNavParams m_params{nullptr};winrt::HaloDesktop::MediaDetail m_detail{nullptr};
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable>m_episodes{nullptr},m_facts{nullptr},m_availability{nullptr},m_seasons{nullptr};
        std::vector<std::int32_t>m_seasonValues;std::int32_t m_seasonIndex{};std::uint32_t m_loadVersion{};bool m_loading{},m_error{},m_inLibrary{};winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler>m_propertyChanged;
    };
}
