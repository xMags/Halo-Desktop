#include "pch.h"
#include "Views/LibraryPage.xaml.h"
#if __has_include("LibraryPage.g.cpp")
#include "LibraryPage.g.cpp"
#endif
#include "App.xaml.h"
#include "Shell/LayoutMetricsService.h"
#include "Controls/PosterCard.xaml.h"
#include "ViewModels/LibraryViewModel.h"

namespace winrt::HaloDesktop::implementation
{
    LibraryPage::LibraryPage() : m_viewModel(winrt::make<LibraryViewModel>(App::Services())) {}
    winrt::HaloDesktop::LibraryViewModel LibraryPage::ViewModel() const { return m_viewModel; }
    void LibraryPage::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_metricsToken == 0)
        {
            if (auto const metrics = App::Services().LayoutMetrics)
            {
                m_metricsToken = metrics->AddChangedHandler([weak = get_weak()]()
                {
                    if (auto self = weak.get()) self->ApplyLayoutMetrics();
                });
            }
        }
        auto const viewModel = winrt::get_self<LibraryViewModel>(m_viewModel);
        m_viewModel.Retry();
        FindName(L"LibraryGrid").as<Microsoft::UI::Xaml::Controls::GridView>().ItemsSource(viewModel->ItemsView());
    }
    void LibraryPage::OnAllFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(0); }
    void LibraryPage::OnMoviesFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(1); }
    void LibraryPage::OnSeriesFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(2); }
    void LibraryPage::OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Retry(); }
    void LibraryPage::OnSortClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const item = sender.as<Microsoft::UI::Xaml::Controls::RadioMenuFlyoutItem>();
        auto const text = item.Text();
        auto const index = text == L"Title A–Z" ? 1 : text == L"Recently watched" ? 2 : 0;
        m_viewModel.SetSort(index);
        SortLabel().Text(text);
    }

    // The cards size themselves; the wrap grid only needs its cell to match, plus
    // the gap between cards and the room the title and meta lines take below the art.

    // The wrap grid's panel does not exist until the grid itself has loaded, so
    // the first sizing pass has to wait for that rather than for the page.
    void LibraryPage::OnGridLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ApplyLayoutMetrics();
    }

    void LibraryPage::ApplyLayoutMetrics()
    {
        auto const metrics = App::Services().LayoutMetrics;
        if (!metrics) return;
        auto const current = metrics->Current();
        auto const grid = FindName(L"LibraryGrid").try_as<Microsoft::UI::Xaml::Controls::GridView>();
        if (!grid) return;
        auto const panel = grid.ItemsPanelRoot().try_as<Microsoft::UI::Xaml::Controls::ItemsWrapGrid>();
        if (!panel) return;
        constexpr double CardGap = 22.0;
        constexpr double CaptionHeight = 52.0;
        panel.ItemWidth(current.PosterWidth + CardGap);
        panel.ItemHeight(current.PosterArtHeight() + CaptionHeight);
    }

    void LibraryPage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_metricsToken == 0) return;
        if (auto const metrics = App::Services().LayoutMetrics) metrics->RemoveChangedHandler(m_metricsToken);
        m_metricsToken = 0;
    }

    void LibraryPage::OnPosterClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenDetail(sender.as<winrt::HaloDesktop::PosterCard>().Tag().as<winrt::HaloDesktop::MediaSummary>()); }
}
