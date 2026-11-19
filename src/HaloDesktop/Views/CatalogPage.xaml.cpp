#include "pch.h"
#include "Views/CatalogPage.xaml.h"
#include "Controls/SmoothWheelScroll.h"
#if __has_include("CatalogPage.g.cpp")
#include "CatalogPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Shell/LayoutMetricsService.h"
#include "Controls/PosterCard.xaml.h"
#include "ViewModels/CatalogViewModel.h"

#include <string_view>

namespace
{
    // How far below the view the next page starts loading.
    constexpr double PrefetchMargin = 800.0;
    // The caption under the art (title and meta lines with their margins) and
    // the band above the card it lifts into on hover.
    constexpr double CaptionHeight = 52.0;
    constexpr double LiftBand = 4.0;
}

namespace winrt::HaloDesktop::implementation
{
    CatalogPage::CatalogPage()
        : m_viewModel(winrt::make<CatalogViewModel>(App::Services())),
          m_titleMenu(App::Services())
    {
    }

    void CatalogPage::OnContextRequested(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args)
    {
        m_titleMenu.ShowForSubject(args);
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
        ::HaloDesktop::Controls::AttachSmoothWheelScroll(CatalogScroller());
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
        // Sized before the grid gets its items, so the first layout already
        // uses the poster width rather than measuring the first card for one.
        ApplyLayoutMetrics();
        if (!m_listBound)
        {
            m_listBound = true;
            auto const viewModel = winrt::get_self<CatalogViewModel>(m_viewModel);
            CatalogGrid().ItemsSource(viewModel->ItemsView());
        }
        // A page that lands, or fails, may leave the end in view, and the
        // scroller would not report a change by itself.
        m_viewModelChanged = m_viewModel.PropertyChanged(winrt::auto_revoke, [weak = get_weak()](
            winrt::Windows::Foundation::IInspectable const&,
            Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& changed)
        {
            auto const self = weak.get();
            if (self && std::wstring_view{ changed.PropertyName() } == L"LoadingMore")
            {
                self->ScheduleNearEndCheck();
            }
        });
    }

    void CatalogPage::OnBackClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_viewModel.GoBack();
    }

    void CatalogPage::OnRetryMoreClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_viewModel.RetryMore();
    }

    // The columns stretch from the poster width; the cards keep their 2:3 art
    // at whatever width that gives them, and the rows grow to match.
    void CatalogPage::ApplyLayoutMetrics()
    {
        auto const metrics = App::Services().LayoutMetrics;
        if (!metrics) return;
        auto const current = metrics->Current();
        auto const layout = CatalogLayout();
        if (!layout) return;
        layout.MinItemWidth(current.PosterWidth);
        layout.MinItemHeight(current.PosterArtHeight() + CaptionHeight + LiftBand);
    }

    void CatalogPage::OnScrollerViewChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args)
    {
        CheckNearEnd();
    }

    void CatalogPage::OnGridSizeChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        CheckNearEnd();
    }

    void CatalogPage::CheckNearEnd()
    {
        auto const scroller = CatalogScroller();
        if (!scroller)
        {
            return;
        }
        if (scroller.ScrollableHeight() - scroller.VerticalOffset() <= PrefetchMargin)
        {
            m_viewModel.RequestMore();
        }
    }

    // After layout rather than now: the titles a page added have not been
    // measured yet when the view model reports it.
    void CatalogPage::ScheduleNearEndCheck()
    {
        auto const queue = DispatcherQueue();
        if (!queue)
        {
            return;
        }
        queue.TryEnqueue(
            Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [weak = get_weak()]()
            {
                if (auto const self = weak.get())
                {
                    self->CheckNearEnd();
                }
            });
    }

    void CatalogPage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModelChanged.revoke();
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
