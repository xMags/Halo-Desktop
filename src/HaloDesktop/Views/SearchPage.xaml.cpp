#include "pch.h"
#include "Views/SearchPage.xaml.h"
#include "Controls/ContentEntrance.h"
#if __has_include("SearchPage.g.cpp")
#include "SearchPage.g.cpp"
#endif
#include "App.xaml.h"
#include "Models/Models.h"
#include "Shell/LayoutMetricsService.h"
#include "ViewModels/SearchViewModel.h"

#include <string_view>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.System.h>

namespace
{
    // The caption under the art (title and meta lines with their margins) and
    // the band above the card it lifts into on hover, as Catalog's grid.
    constexpr double CaptionHeight = 52.0;
    constexpr double LiftBand = 4.0;

    winrt::Microsoft::UI::Xaml::DependencyObject FindNamedDescendant(
        winrt::Microsoft::UI::Xaml::DependencyObject const& root,
        std::wstring_view name)
    {
        auto const count = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChildrenCount(root);
        for (std::int32_t index = 0; index < count; ++index)
        {
            auto const child = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChild(root, index);
            if (auto const element = child.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
                element && std::wstring_view{ element.Name() } == name)
            {
                return child;
            }
            if (auto found = FindNamedDescendant(child, name))
            {
                return found;
            }
        }
        return nullptr;
    }

    // Whether a tap landed on one of the card's own buttons, which act for themselves.
    bool FromButton(winrt::Windows::Foundation::IInspectable const& source, winrt::Microsoft::UI::Xaml::UIElement const& card)
    {
        auto node = source.try_as<winrt::Microsoft::UI::Xaml::DependencyObject>();
        while (node)
        {
            if (node.try_as<winrt::Microsoft::UI::Xaml::Controls::Primitives::ButtonBase>())
            {
                return true;
            }
            if (node == card)
            {
                return false;
            }
            node = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(node);
        }
        return false;
    }

    winrt::hstring TagText(winrt::Windows::Foundation::IInspectable const& sender)
    {
        auto const element = sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
        if (!element) return {};
        if (auto const value = element.Tag().try_as<winrt::Windows::Foundation::IPropertyValue>())
        {
            return value.GetString();
        }
        return {};
    }
}

namespace winrt::HaloDesktop::implementation
{
    SearchPage::SearchPage() : m_viewModel(winrt::make<SearchViewModel>(App::Services())), m_titleMenu(App::Services()) {}

    winrt::HaloDesktop::SearchViewModel SearchPage::ViewModel() const { return m_viewModel; }

    // Opening a result from its menu records the search the way clicking it does.
    void SearchPage::OnContextRequested(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args)
    {
        m_titleMenu.ShowForSubject(args, [viewModel = m_viewModel]()
        {
            winrt::get_self<SearchViewModel>(viewModel)->RecordQuery();
        });
    }

    void SearchPage::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_metricsToken == 0)
        {
            if (auto const metrics = App::Services().LayoutMetrics)
            {
                m_metricsToken = metrics->AddChangedHandler([weak = get_weak()]()
                {
                    if (auto const self = weak.get()) self->ApplyLayoutMetrics();
                });
            }
        }
        // Sized before the grids get their items, so the first layout already
        // uses the poster width.
        ApplyLayoutMetrics();
        EnsureSkeletonPulse();
        // Bound once for the life of the page: handing a repeater the same
        // collection again resets it, and this runs on every return.
        if (!m_listsBound)
        {
            m_listsBound = true;
            auto const viewModel = winrt::get_self<SearchViewModel>(m_viewModel);
            SuggestionList().ItemsSource(viewModel->SuggestionsView());
            RecentList().ItemsSource(viewModel->RecentItemsView());
            PopularGrid().ItemsSource(viewModel->PopularItemsView());
            LibraryGrid().ItemsSource(viewModel->LibraryItemsView());
            ResultsGrid().ItemsSource(viewModel->ResultItemsView());
        }
        m_viewModelChanged = m_viewModel.PropertyChanged(winrt::auto_revoke, [weak = get_weak()](
            winrt::Windows::Foundation::IInspectable const&,
            Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& changed)
        {
            auto const self = weak.get();
            if (!self) return;
            if (std::wstring_view{ changed.PropertyName() } == L"ShowKindBadges")
            {
                self->ApplyKindBadges(self->PopularGrid());
                self->ApplyKindBadges(self->LibraryGrid());
                self->ApplyKindBadges(self->ResultsGrid());
            }
            self->UpdateSkeletonPulse();
            ::HaloDesktop::Controls::PlayEntranceWhenShown(self->PopularGrid(), self->m_popularShown);
            // A query's results settle in when they arrive, which is when its
            // placeholders go, never on each keystroke.
            auto const loading = self->ResultsSkeleton().Visibility() == Microsoft::UI::Xaml::Visibility::Visible;
            if (self->m_resultsLoading && !loading) ::HaloDesktop::Controls::PlayContentEntrance(self->ResultsGrid());
            self->m_resultsLoading = loading;
        });
        m_onScreen = true;
        m_viewModel.Shown();
        UpdateSkeletonPulse();

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

    void SearchPage::OnUnloaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModelChanged.revoke();
        m_onScreen = false;
        UpdateSkeletonPulse();
        if (m_metricsToken == 0) return;
        if (auto const metrics = App::Services().LayoutMetrics) metrics->RemoveChangedHandler(m_metricsToken);
        m_metricsToken = 0;
    }

    void SearchPage::OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args)
    {
        m_focusQueryOnLoaded = args.NavigationMode() != Microsoft::UI::Xaml::Navigation::NavigationMode::Back;
        if (auto const query = args.Parameter().try_as<winrt::Windows::Foundation::IPropertyValue>())
        {
            m_viewModel.Arrive(query.GetString());
        }
    }

    // WinUI's box shows its own clear button only while it has the caret. The
    // page draws one that stays whenever there is text, so the built-in one is
    // given no width; its visual states only ever change its visibility.
    void SearchPage::OnQueryBoxLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto const builtIn = FindNamedDescendant(QueryBox(), L"DeleteButton").try_as<Microsoft::UI::Xaml::FrameworkElement>())
        {
            builtIn.Width(0.0);
            builtIn.MinWidth(0.0);
            builtIn.Margin({});
            builtIn.IsHitTestVisible(false);
            if (auto const control = builtIn.try_as<Microsoft::UI::Xaml::Controls::Control>()) control.IsTabStop(false);
        }
    }

    void SearchPage::OnQueryKeyDown([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() == winrt::Windows::System::VirtualKey::Enter)
        {
            m_viewModel.Submit();
            args.Handled(true);
            return;
        }
        if (args.Key() == winrt::Windows::System::VirtualKey::Escape && !m_viewModel.Query().empty())
        {
            m_viewModel.ClearQuery();
            args.Handled(true);
        }
    }

    void SearchPage::OnQueryGotFocus([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetBoxFocused(true); }
    void SearchPage::OnQueryLostFocus([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetBoxFocused(false); }

    void SearchPage::OnClearClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.ClearQuery();
        FocusQueryBox();
    }

    void SearchPage::OnSuggestionClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SearchAgain(TagText(sender));
        FocusQueryBox();
    }

    void SearchPage::OnRecentClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SearchAgain(TagText(sender));
        FocusQueryBox();
    }

    void SearchPage::OnForgetClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Forget(TagText(sender)); }
    void SearchPage::OnClearRecentClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.ForgetAll(); }

    void SearchPage::OnShowAllResultsClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        AllChip().IsChecked(true);
        m_viewModel.ShowAllResults();
    }

    void SearchPage::OnAllFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(0); }
    void SearchPage::OnMoviesFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(1); }
    void SearchPage::OnSeriesFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(2); }

    void SearchPage::OnPosterClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto const card = sender.try_as<winrt::HaloDesktop::PosterCard>())
        {
            m_viewModel.OpenDetail(card.Tag());
        }
    }

    void SearchPage::OnPosterFailed(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        // The card names the art that failed; its Tag names the title it was last
        // handed. A recycled card can hold one item's art under another's Tag, so
        // the title is dropped only when both name the same item.
        auto const card = sender.try_as<winrt::HaloDesktop::PosterCard>();
        if (!card) return;
        auto const media = card.Tag().try_as<winrt::HaloDesktop::MediaSummary>();
        if (!media || card.MetaId() != media.Id() || card.Poster() != media.Poster()) return;
        m_viewModel.ReportMissingArt(media);
    }

    void SearchPage::OnGridElementPrepared(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::ItemsRepeater const&,
        Microsoft::UI::Xaml::Controls::ItemsRepeaterElementPreparedEventArgs const& args)
    {
        if (auto const card = args.Element().try_as<winrt::HaloDesktop::PosterCard>())
        {
            card.ShowKindBadge(m_viewModel.ShowKindBadges());
        }
    }

    // The badge says the kind only while every kind is listed.
    void SearchPage::ApplyKindBadges(Microsoft::UI::Xaml::Controls::ItemsRepeater const& grid)
    {
        if (!grid) return;
        auto const show = m_viewModel.ShowKindBadges();
        auto const count = Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChildrenCount(grid);
        for (std::int32_t index = 0; index < count; ++index)
        {
            if (auto const card = Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChild(grid, index).try_as<winrt::HaloDesktop::PosterCard>())
            {
                card.ShowKindBadge(show);
            }
        }
    }

    void SearchPage::OnTopMatchTapped(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args)
    {
        if (FromButton(args.OriginalSource(), TopMatchCard()))
        {
            return;
        }
        m_viewModel.OpenTopMatch();
    }

    void SearchPage::OnTopMatchPointerEntered(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        TopMatchHoverRing().Opacity(1.0);
    }

    void SearchPage::OnTopMatchPointerExited(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        TopMatchHoverRing().Opacity(0.0);
    }

    void SearchPage::OnTopMatchContentSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        // The content decides the card's height; the art fills exactly that.
        TopMatchArt().Height(args.NewSize().Height);
    }

    void SearchPage::OnTopMatchPlayClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.PlayTopMatch(); }
    void SearchPage::OnTopMatchDetailsClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenTopMatch(); }

    void SearchPage::ApplyLayoutMetrics()
    {
        auto const metrics = App::Services().LayoutMetrics;
        if (!metrics) return;
        auto const current = metrics->Current();
        PageBody().Padding({ current.Gutter, 0.0, current.Gutter, 40.0 });
        PageTitle().Margin({ current.Gutter, 28.0, current.Gutter, 10.0 });
        for (auto const& layout : { PopularLayout(), LibraryLayout(), ResultsLayout() })
        {
            if (!layout) continue;
            layout.MinItemWidth(current.PosterWidth);
            layout.MinItemHeight(current.PosterArtHeight() + CaptionHeight + LiftBand);
        }
        auto const posterColumn = Microsoft::UI::Xaml::GridLengthHelper::FromPixels(current.PosterWidth);
        TopMatchPosterColumn().Width(posterColumn);
        TopMatchSkeletonColumn().Width(posterColumn);
        TopMatchPoster().Height(current.PosterArtHeight());
        TopMatchSkeletonPoster().Height(current.PosterArtHeight());
    }

    void SearchPage::EnsureSkeletonPulse()
    {
        if (m_skeletonPulse) return;
        auto const compositor = Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(PopularSkeleton()).Compositor();
        m_skeletonPulse = std::make_unique<::HaloDesktop::Controls::SkeletonPulse>(compositor);
        m_skeletonPulse->Attach(PopularSkeleton());
        m_skeletonPulse->Attach(TopMatchSkeletonPoster());
        m_skeletonPulse->Attach(TopMatchSkeletonBody());
        m_skeletonPulse->Attach(ResultsSkeleton());
    }

    void SearchPage::UpdateSkeletonPulse()
    {
        if (!m_skeletonPulse) return;
        m_skeletonPulse->SetActive(m_onScreen && winrt::get_self<SearchViewModel>(m_viewModel)->SkeletonsShown());
    }

    void SearchPage::FocusQueryBox()
    {
        auto const box = QueryBox();
        box.Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
        box.SelectionStart(static_cast<std::int32_t>(box.Text().size()));
    }
}
