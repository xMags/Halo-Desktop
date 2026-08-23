#include "pch.h"
#include "Views/DetailPage.xaml.h"
#if __has_include("DetailPage.g.cpp")
#include "DetailPage.g.cpp"
#endif

#include "App.xaml.h"
#include "ViewModels/DetailViewModel.h"

namespace winrt::HaloDesktop::implementation
{
    DetailPage::DetailPage()
        : m_viewModel(winrt::make<DetailViewModel>(App::Services()))
    {
    }
    winrt::HaloDesktop::DetailViewModel DetailPage::ViewModel() const { return m_viewModel; }
    void DetailPage::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const viewModel = winrt::get_self<DetailViewModel>(m_viewModel);
        FindName(L"EpisodeList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->EpisodesView());
        FindName(L"FactsList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->FactsView());
        FindName(L"AvailabilityList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->AvailabilityView());
    }
    void DetailPage::OnSeasonOneClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SelectSeason(0); }
    void DetailPage::OnSeasonTwoClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SelectSeason(1); }
    void DetailPage::OnEpisodeClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenSources(); }
    void DetailPage::OnBrowseSourcesClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenSources(); }
    void DetailPage::OnResumeClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenPlayer(); }
    void DetailPage::OnDownloadsClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenDownloads(); }
}
