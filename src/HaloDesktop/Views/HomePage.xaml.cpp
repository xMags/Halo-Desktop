#include "pch.h"
#include "Views/HomePage.xaml.h"
#if __has_include("HomePage.g.cpp")
#include "HomePage.g.cpp"
#endif

#include "App.xaml.h"
#include "ViewModels/HomeViewModel.h"

namespace winrt::HaloDesktop::implementation
{
    HomePage::HomePage()
        : m_viewModel(winrt::make<HomeViewModel>(App::Services()))
    {
    }

    winrt::HaloDesktop::HomeViewModel HomePage::ViewModel() const
    {
        return m_viewModel;
    }

    void HomePage::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const viewModel = winrt::get_self<HomeViewModel>(m_viewModel);
        FindName(L"ContinueList")
            .as<Microsoft::UI::Xaml::Controls::ItemsControl>()
            .ItemsSource(viewModel->ContinueItemsView());
        FindName(L"ShelfList")
            .as<Microsoft::UI::Xaml::Controls::ItemsControl>()
            .ItemsSource(viewModel->ShelvesView());
    }

    void HomePage::OnSearchSubmitted(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
        Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const& args)
    {
        m_viewModel.OpenSearch(args.QueryText());
    }

    void HomePage::OnAllFilterClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_viewModel.SetFilter(0);
    }

    void HomePage::OnMoviesFilterClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_viewModel.SetFilter(1);
    }

    void HomePage::OnSeriesFilterClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_viewModel.SetFilter(2);
    }

    void HomePage::OnResumeClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_viewModel.OpenPlayer();
    }

    void HomePage::OnDetailsClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_viewModel.OpenDetail();
    }

    void HomePage::OnContinueItemClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_viewModel.OpenPlayer();
    }

    void HomePage::OnShelfItemClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_viewModel.OpenDetail();
    }
}
