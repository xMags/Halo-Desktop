#include "pch.h"
#include "Views/CatalogPage.xaml.h"
#if __has_include("CatalogPage.g.cpp")
#include "CatalogPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Shell/LayoutMetricsService.h"
#include "Controls/PosterCard.xaml.h"
#include "ViewModels/CatalogViewModel.h"

namespace winrt::HaloDesktop::implementation
{
    CatalogPage::CatalogPage()
        : m_viewModel(winrt::make<CatalogViewModel>(App::Services()))
    {
    }

    winrt::HaloDesktop::CatalogViewModel CatalogPage::ViewModel() const
    {
        return m_viewModel;
    }

    void CatalogPage::OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args)
    {
        m_viewModel.Load(args.Parameter());
    }

    void CatalogPage::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
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
        auto const viewModel = winrt::get_self<CatalogViewModel>(m_viewModel);
        FindName(L"CatalogGrid")
            .as<Microsoft::UI::Xaml::Controls::GridView>()
            .ItemsSource(viewModel->ItemsView());
    }

    void CatalogPage::OnBackClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_viewModel.GoBack();
    }


    // The cards size themselves; the wrap grid only needs its cell to match, plus
    // the gap between cards and the room the title and meta lines take below the art.

    // The wrap grid's panel does not exist until the grid itself has loaded, so
    // the first sizing pass has to wait for that rather than for the page.
    void CatalogPage::OnGridLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ApplyLayoutMetrics();
    }

    void CatalogPage::ApplyLayoutMetrics()
    {
        auto const metrics = App::Services().LayoutMetrics;
        if (!metrics) return;
        auto const current = metrics->Current();
        auto const grid = FindName(L"CatalogGrid").try_as<Microsoft::UI::Xaml::Controls::GridView>();
        if (!grid) return;
        auto const panel = grid.ItemsPanelRoot().try_as<Microsoft::UI::Xaml::Controls::ItemsWrapGrid>();
        if (!panel) return;
        constexpr double CardGap = 22.0;
        constexpr double CaptionHeight = 52.0;
        panel.ItemWidth(current.PosterWidth + CardGap);
        panel.ItemHeight(current.PosterArtHeight() + CaptionHeight);
    }

    void CatalogPage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_metricsToken == 0) return;
        if (auto const metrics = App::Services().LayoutMetrics) metrics->RemoveChangedHandler(m_metricsToken);
        m_metricsToken = 0;
    }

    void CatalogPage::OnPosterClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const card = sender.try_as<winrt::HaloDesktop::PosterCard>();
        if (!card)
        {
            return;
        }

        m_viewModel.OpenDetail(card.Tag());
    }
}
