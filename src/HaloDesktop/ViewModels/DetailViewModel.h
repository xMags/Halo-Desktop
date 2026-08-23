#pragma once

#include "DetailEpisodeViewModel.g.h"
#include "DetailViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"

#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    struct DetailEpisodeViewModel : DetailEpisodeViewModelT<DetailEpisodeViewModel>
    {
        explicit DetailEpisodeViewModel(winrt::HaloDesktop::Episode episode);
        [[nodiscard]] winrt::hstring Tag() const;
        [[nodiscard]] winrt::hstring Title() const;
        [[nodiscard]] winrt::hstring Blurb() const;
        [[nodiscard]] winrt::hstring Runtime() const;
        [[nodiscard]] winrt::hstring Aired() const;
        [[nodiscard]] double Progress() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SavedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility InProgressVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility IdleVisibility() const noexcept;

    private:
        winrt::HaloDesktop::Episode m_episode{ nullptr };
    };

    struct DetailViewModel : DetailViewModelT<DetailViewModel>
    {
        explicit DetailViewModel(::HaloDesktop::Services::AppServices const& services);
        [[nodiscard]] winrt::hstring Title() const;
        [[nodiscard]] winrt::hstring Kicker() const;
        [[nodiscard]] winrt::hstring MetaLine() const;
        [[nodiscard]] winrt::hstring Synopsis() const;
        [[nodiscard]] winrt::hstring SeasonMeta() const;
        [[nodiscard]] std::int32_t SeasonIndex() const noexcept;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Episodes() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Facts() const;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Availability() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> EpisodesView() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> FactsView() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> AvailabilityView() const;
        void SelectSeason(std::int32_t index);
        void OpenSources();
        void OpenPlayer();
        void OpenDownloads();
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void RebuildEpisodes();
        std::shared_ptr<::HaloDesktop::Services::IMetadataService> m_metadata;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::HaloDesktop::MediaDetail m_detail{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_episodes{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_facts{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_availability{ nullptr };
        std::int32_t m_seasonIndex{ 1 };
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
