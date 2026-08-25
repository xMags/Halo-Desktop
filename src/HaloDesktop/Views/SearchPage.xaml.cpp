#include "pch.h"
#include "Views/SearchPage.xaml.h"
#if __has_include("SearchPage.g.cpp")
#include "SearchPage.g.cpp"
#endif
#include "App.xaml.h"
#include "Controls/MediaShelf.xaml.h"
#include "Models/Models.h"
#include "ViewModels/SearchViewModel.h"

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
    SearchPage::SearchPage() : m_viewModel(winrt::make<SearchViewModel>(App::Services())) {}
    winrt::HaloDesktop::SearchViewModel SearchPage::ViewModel() const { return m_viewModel; }
    void SearchPage::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const viewModel = winrt::get_self<SearchViewModel>(m_viewModel);
        FindName(L"ResultsList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->ResultsView());
        FindName(L"RecentList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->RecentItemsView());

        if (!m_focusQueryOnLoaded)
        {
            return;
        }

        // A normal arrival takes the caret. Back navigation leaves focus alone so the
        // cached page can retain its scroll position. NavigationView focuses its invoked
        // menu item after navigation, so this is queued behind that focus change.
        DispatcherQueue().TryEnqueue(
            Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [weak = get_weak()]()
            {
                if (auto const self = weak.get())
                {
                    self->QueryBox().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
                }
            });
    }
    void SearchPage::OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args)
    {
        m_focusQueryOnLoaded = args.NavigationMode() != Microsoft::UI::Xaml::Navigation::NavigationMode::Back;
        if (auto query = args.Parameter().try_as<winrt::Windows::Foundation::IPropertyValue>())
        {
            m_viewModel.Submit(query.GetString());
        }
    }
    void SearchPage::OnQuerySubmitted(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
        Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const& args)
    {
        m_viewModel.Submit(args.QueryText());
    }
    void SearchPage::OnAllFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(0); }
    void SearchPage::OnMoviesFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(1); }
    void SearchPage::OnSeriesFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(2); }
    void SearchPage::OnClearClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Clear(); }
    void SearchPage::OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Retry(); }
    void SearchPage::OnTopMatchClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenTopMatch(); }
    void SearchPage::OnShelfItemClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenDetail(sender.as<winrt::HaloDesktop::MediaShelf>().SelectedItem().as<winrt::HaloDesktop::MediaSummary>()); }
    void SearchPage::OnShelfSeeAllClick(
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
    void SearchPage::OnRecentClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_viewModel.Submit(winrt::unbox_value<winrt::hstring>(sender.as<Microsoft::UI::Xaml::Controls::Button>().Tag())); }
}
