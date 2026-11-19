#include "pch.h"
#include "Views/LibraryPage.xaml.h"
#include "Controls/ContentEntrance.h"
#if __has_include("LibraryPage.g.cpp")
#include "LibraryPage.g.cpp"
#endif
#include "App.xaml.h"
#include "Shell/LayoutMetricsService.h"
#include "Controls/PosterCard.xaml.h"
#include "ViewModels/LibraryViewModel.h"

#include <vector>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>

namespace
{
    // The caption under the art (title and meta lines with their margins) and
    // the band above the card it lifts into on hover, as Catalog's grid.
    constexpr double CaptionHeight = 52.0;
    constexpr double LiftBand = 4.0;
    // The reference draws twelve poster skeletons while the library loads.
    constexpr int SkeletonCount = 12;
}

namespace winrt::HaloDesktop::implementation
{
    LibraryPage::LibraryPage() : m_viewModel(winrt::make<LibraryViewModel>(App::Services())), m_titleMenu(App::Services()) {}
    void LibraryPage::OnContextRequested([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args)
    {
        m_titleMenu.ShowForSubject(args);
    }
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
        // Sized before the grid gets its items, so the first layout already uses
        // the poster width rather than measuring the first card for one.
        ApplyLayoutMetrics();
        EnsureSkeletonPulse();
        // Bound once for the life of the page: handing a repeater the same
        // collection again resets it, and this runs on every return.
        if (!m_listBound)
        {
            m_listBound = true;
            auto const viewModel = winrt::get_self<LibraryViewModel>(m_viewModel);
            LibraryGrid().ItemsSource(viewModel->ItemsView());
            std::vector<winrt::Windows::Foundation::IInspectable> placeholders;
            placeholders.reserve(SkeletonCount);
            for (int index = 0; index < SkeletonCount; ++index) placeholders.push_back(winrt::box_value(index));
            SkeletonGrid().ItemsSource(winrt::single_threaded_vector(std::move(placeholders)));
        }
        m_viewModelChanged = m_viewModel.PropertyChanged(winrt::auto_revoke, [weak = get_weak()](
            winrt::Windows::Foundation::IInspectable const&,
            Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const&)
        {
            auto const self = weak.get();
            if (!self) return;
            self->UpdateSkeletonPulse();
            ::HaloDesktop::Controls::PlayEntranceWhenShown(self->LibraryGrid(), self->m_gridShown);
        });
        m_onScreen = true;
        m_viewModel.Retry();
        UpdateSkeletonPulse();
    }
    // The tabs in the view model's order: All, Movies, Series.
    void LibraryPage::OnFilterSelectionChanged(
        Microsoft::UI::Xaml::Controls::SelectorBar const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&)
    {
        auto const selected = sender.SelectedItem();
        if (selected == AllFilterItem()) m_viewModel.SetFilter(0);
        else if (selected == MoviesFilterItem()) m_viewModel.SetFilter(1);
        else if (selected == SeriesFilterItem()) m_viewModel.SetFilter(2);
        else return;
        ::HaloDesktop::Controls::PlayContentEntrance(LibraryGrid());
    }
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

    // The columns stretch from the poster width; the cards keep their 2:3 art at
    // whatever width that gives them, and the rows grow to match. The page's
    // sides follow the layout step's gutter, as the reference's `--gu` does.
    void LibraryPage::ApplyLayoutMetrics()
    {
        auto const metrics = App::Services().LayoutMetrics;
        if (!metrics) return;
        auto const current = metrics->Current();
        auto const gutter = current.Gutter;
        LibraryHeader().Padding({ gutter, 28.0, gutter, 4.0 });
        for (auto const& note : { ErrorNote().as<Microsoft::UI::Xaml::FrameworkElement>(), EmptyNote().as<Microsoft::UI::Xaml::FrameworkElement>(), FilteredEmptyNote().as<Microsoft::UI::Xaml::FrameworkElement>() })
        {
            note.Margin({ gutter, 14.0, gutter, 14.0 });
        }
        LibraryGrid().Margin({ gutter, 16.0, gutter, 32.0 });
        SkeletonGrid().Margin({ gutter, 16.0, gutter, 32.0 });
        for (auto const& layout : { LibraryLayout(), SkeletonLayout() })
        {
            if (!layout) continue;
            layout.MinItemWidth(current.PosterWidth);
            layout.MinItemHeight(current.PosterArtHeight() + CaptionHeight + LiftBand);
        }
    }

    void LibraryPage::EnsureSkeletonPulse()
    {
        if (m_skeletonPulse) return;
        auto const compositor = Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(LibrarySkeleton()).Compositor();
        m_skeletonPulse = std::make_unique<::HaloDesktop::Controls::SkeletonPulse>(compositor);
        m_skeletonPulse->Attach(LibrarySkeleton());
    }

    void LibraryPage::UpdateSkeletonPulse()
    {
        if (!m_skeletonPulse) return;
        m_skeletonPulse->SetActive(m_onScreen && winrt::get_self<LibraryViewModel>(m_viewModel)->SkeletonsShown());
    }

    void LibraryPage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModelChanged.revoke();
        m_onScreen = false;
        UpdateSkeletonPulse();
        if (m_metricsToken == 0) return;
        if (auto const metrics = App::Services().LayoutMetrics) metrics->RemoveChangedHandler(m_metricsToken);
        m_metricsToken = 0;
    }

    void LibraryPage::OnPosterClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenDetail(sender.as<winrt::HaloDesktop::PosterCard>().Tag().as<winrt::HaloDesktop::MediaSummary>()); }
}
