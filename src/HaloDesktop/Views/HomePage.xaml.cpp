#include "pch.h"
#include "Views/HomePage.xaml.h"
#if __has_include("HomePage.g.cpp")
#include "HomePage.g.cpp"
#endif

#include "App.xaml.h"
#include "Controls/MediaShelf.xaml.h"
#include "Models/Models.h"
#include "Services/NavigationService.h"
#include "ViewModels/HomeViewModel.h"

#include <vector>

namespace
{
    winrt::HaloDesktop::Shelf CreateShelfSnapshot(winrt::HaloDesktop::MediaShelf const& shelf)
    {
        std::vector<winrt::HaloDesktop::MediaSummary> items;
        if (auto const currentItems = shelf.ItemsSource().try_as<
                winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary>>())
        {
            items.reserve(currentItems.Size());
            for (auto const& item : currentItems)
            {
                if (item)
                {
                    items.push_back(item);
                }
            }
        }

        return winrt::make<winrt::HaloDesktop::implementation::Shelf>(
            shelf.Title(),
            shelf.SourceLabel(),
            winrt::single_threaded_vector(std::move(items)).GetView());
    }
}

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
        m_viewModel.Retry();
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
    void HomePage::OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Retry(); }
    void HomePage::OnOpenSettingsClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { App::Services().Navigation->GoTo(::HaloDesktop::Services::Page::Settings); }

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
        m_viewModel.OpenHeroSources();
    }

    void HomePage::OnDetailsClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_viewModel.OpenHeroDetail();
    }

    void HomePage::OnContinueItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const item = winrt::unbox_value<winrt::HaloDesktop::ContinueItem>(sender.as<Microsoft::UI::Xaml::Controls::Button>().Tag());
        m_viewModel.OpenContinue(item);
    }

    void HomePage::OnContinueScrollLeft(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        ScrollContinueBy(-1.0);
    }

    void HomePage::OnContinueScrollRight(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        ScrollContinueBy(1.0);
    }

    void HomePage::ScrollContinueBy(double direction)
    {
        auto const scroller = FindName(L"ContinueScroller").as<Microsoft::UI::Xaml::Controls::ScrollViewer>();
        scroller.ChangeView(
            scroller.HorizontalOffset() + direction * scroller.ViewportWidth() * 0.8,
            nullptr,
            nullptr,
            false);
    }

    void HomePage::OnShelfItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const shelf = sender.as<winrt::HaloDesktop::MediaShelf>();
        m_viewModel.OpenDetail(shelf.SelectedItem().as<winrt::HaloDesktop::MediaSummary>());
    }

    void HomePage::OnContinueSeeAllClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_viewModel.OpenContinueCatalog();
    }

    void HomePage::OnShelfSeeAllClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const shelf = sender.try_as<winrt::HaloDesktop::MediaShelf>();
        if (!shelf)
        {
            return;
        }

        m_viewModel.OpenCatalog(CreateShelfSnapshot(shelf));
    }
}
