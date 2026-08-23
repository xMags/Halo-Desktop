#include "pch.h"
#include "Views/SearchPage.xaml.h"
#if __has_include("SearchPage.g.cpp")
#include "SearchPage.g.cpp"
#endif
#include "App.xaml.h"
#include "ViewModels/SearchViewModel.h"

namespace winrt::HaloDesktop::implementation
{
    SearchPage::SearchPage() : m_viewModel(winrt::make<SearchViewModel>(App::Services())) {}
    winrt::HaloDesktop::SearchViewModel SearchPage::ViewModel() const { return m_viewModel; }
    void SearchPage::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const viewModel = winrt::get_self<SearchViewModel>(m_viewModel);
        FindName(L"ResultsList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->ResultsView());
        FindName(L"RecentList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->RecentItemsView());

        // Search is the shortcut's only entry point, so it takes the caret on arrival.
        // NavigationView focuses the invoked menu item after navigation completes, so this
        // is queued behind that rather than run inline, which it would otherwise undo.
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
    void SearchPage::OnPeopleFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(3); }
    void SearchPage::OnClearClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Clear(); }
    void SearchPage::OnTopMatchClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenDetail(); }
    void SearchPage::OnShelfItemClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenDetail(); }
    void SearchPage::OnRecentClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_viewModel.Submit(winrt::unbox_value<winrt::hstring>(sender.as<Microsoft::UI::Xaml::Controls::Button>().Tag())); }
}
