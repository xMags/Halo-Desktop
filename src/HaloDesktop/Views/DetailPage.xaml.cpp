#include "pch.h"
#include "Views/DetailPage.xaml.h"
#if __has_include("DetailPage.g.cpp")
#include "DetailPage.g.cpp"
#endif

#include "App.xaml.h"
#include "ViewModels/DetailViewModel.h"

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace winrt::HaloDesktop::implementation
{
    DetailPage::DetailPage()
        : m_viewModel(winrt::make<DetailViewModel>(App::Services()))
    {
    }
    winrt::HaloDesktop::DetailViewModel DetailPage::ViewModel() const { return m_viewModel; }
    void DetailPage::OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args){m_viewModel.Load(args.Parameter());}
    void DetailPage::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const viewModel = winrt::get_self<DetailViewModel>(m_viewModel);
        viewModel->Activate();
        FindName(L"EpisodeList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->EpisodesView());
        FindName(L"FactsList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->FactsView());
        FindName(L"AvailabilityList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->AvailabilityView());
        FindName(L"SeasonPicker").as<Microsoft::UI::Xaml::Controls::ComboBox>().ItemsSource(viewModel->SeasonsView());
        if (m_viewModelChangedToken.value == 0)
        {
            m_viewModelChangedToken = m_viewModel.PropertyChanged([weak = get_weak()](auto const&, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args)
            {
                if ((args.PropertyName() == L"Poster" || args.PropertyName() == L"Background"))
                {
                    if (auto const self = weak.get()) self->RefreshArtwork();
                }
            });
        }
        RefreshArtwork();
    }
    void DetailPage::OnUnloaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,[[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&){if(m_viewModelChangedToken.value!=0){m_viewModel.PropertyChanged(m_viewModelChangedToken);m_viewModelChangedToken={};}winrt::get_self<DetailViewModel>(m_viewModel)->Deactivate();}
    void DetailPage::OnSeasonOneClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SelectSeason(0); }
    void DetailPage::OnSeasonTwoClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SelectSeason(1); }
    void DetailPage::OnSeasonChanged(winrt::Windows::Foundation::IInspectable const& sender,Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&){m_viewModel.SelectSeason(sender.as<Microsoft::UI::Xaml::Controls::ComboBox>().SelectedIndex());}
    void DetailPage::OnRetryClick(winrt::Windows::Foundation::IInspectable const&,Microsoft::UI::Xaml::RoutedEventArgs const&){m_viewModel.Retry();}
    void DetailPage::OnLibraryClick(winrt::Windows::Foundation::IInspectable const&,Microsoft::UI::Xaml::RoutedEventArgs const&){m_viewModel.ToggleLibrary();}
    void DetailPage::OnEpisodeClick(winrt::Windows::Foundation::IInspectable const& sender,Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenSources(sender.as<Microsoft::UI::Xaml::Controls::Button>().Tag()); }
    void DetailPage::OnBrowseSourcesClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.BrowseSources(); }
    void DetailPage::OnResumeClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenPrimarySource(); }
    void DetailPage::OnDownloadsClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.BrowseSources(); }
    void DetailPage::OnArtworkOpened(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&) { sender.as<Microsoft::UI::Xaml::Controls::Image>().Opacity(1); }
    void DetailPage::OnArtworkFailed(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::ExceptionRoutedEventArgs const&) { sender.as<Microsoft::UI::Xaml::Controls::Image>().Opacity(0); }
    void DetailPage::RefreshArtwork()
    {
        auto setSource = [](Microsoft::UI::Xaml::Controls::Image const& image, winrt::hstring const& value)
        {
            image.Opacity(0);
            if (value.empty())
            {
                image.Source(nullptr);
                return;
            }
            try { image.Source(Microsoft::UI::Xaml::Media::Imaging::BitmapImage{ winrt::Windows::Foundation::Uri{ value } }); }
            catch (...) { image.Source(nullptr); }
        };
        setSource(FindName(L"PosterImage").as<Microsoft::UI::Xaml::Controls::Image>(), m_viewModel.Poster());
        setSource(FindName(L"BackdropImage").as<Microsoft::UI::Xaml::Controls::Image>(), m_viewModel.Background());
    }
}
