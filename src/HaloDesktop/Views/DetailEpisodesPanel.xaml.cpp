#include "pch.h"
#include "Views/DetailEpisodesPanel.xaml.h"
#if __has_include("DetailEpisodesPanel.g.cpp")
#include "DetailEpisodesPanel.g.cpp"
#endif

namespace winrt::HaloDesktop::implementation
{
    // The view model ignores the selections the pickers report while it refills
    // them, and a selection of what is already shown.
    void DetailEpisodesPanel::OnSeasonChanged(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&)
    {
        if (!m_viewModel) return;
        m_viewModel.SelectSeason(sender.as<Microsoft::UI::Xaml::Controls::ComboBox>().SelectedIndex());
    }

    void DetailEpisodesPanel::OnPageChanged(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&)
    {
        if (!m_viewModel) return;
        m_viewModel.SelectPage(sender.as<Microsoft::UI::Xaml::Controls::ComboBox>().SelectedIndex());
    }

    void DetailEpisodesPanel::OnEpisodeClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (!m_viewModel) return;
        m_viewModel.OpenEpisode(sender.as<winrt::HaloDesktop::DetailEpisodeRow>().Episode());
    }
}
