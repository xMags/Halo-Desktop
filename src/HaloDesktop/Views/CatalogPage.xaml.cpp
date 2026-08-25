#include "pch.h"
#include "Views/CatalogPage.xaml.h"
#if __has_include("CatalogPage.g.cpp")
#include "CatalogPage.g.cpp"
#endif

#include "App.xaml.h"
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
        auto const viewModel = winrt::get_self<CatalogViewModel>(m_viewModel);
        FindName(L"CatalogGrid")
            .as<Microsoft::UI::Xaml::Controls::GridView>()
            .ItemsSource(viewModel->ItemsView());
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
