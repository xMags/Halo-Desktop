#pragma once

#include "DetailEpisodesPanel.g.h"

namespace winrt::HaloDesktop::implementation
{
    struct DetailEpisodesPanel : DetailEpisodesPanelT<DetailEpisodesPanel>
    {
        DetailEpisodesPanel() = default;

        [[nodiscard]] winrt::HaloDesktop::DetailViewModel ViewModel() const { return m_viewModel; }
        // Set once by the page before the control loads.
        void ViewModel(winrt::HaloDesktop::DetailViewModel const& value) { m_viewModel = value; }

        void OnSeasonChanged(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnPageChanged(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnEpisodeClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        winrt::HaloDesktop::DetailViewModel m_viewModel{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct DetailEpisodesPanel : DetailEpisodesPanelT<DetailEpisodesPanel, implementation::DetailEpisodesPanel>
    {
    };
}
