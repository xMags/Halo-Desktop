#include "pch.h"
#include "Views/SearchPage.xaml.h"
#include "Controls/SmoothWheelScroll.h"
#include "Controls/ContentEntrance.h"
#if __has_include("SearchPage.g.cpp")
#include "SearchPage.g.cpp"
#endif
#include "App.xaml.h"
#include "Models/Models.h"
#include "Shell/LayoutMetricsService.h"
#include "ViewModels/SearchViewModel.h"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.System.h>

namespace
{
    // The caption under the art (title and meta lines with their margins) and
    // the band above the card it lifts into on hover, as Catalog's grid.
    constexpr double CaptionHeight = 52.0;
    constexpr double LiftBand = 4.0;
    // What an earlier query's results fade to while the next one's are awaited.
    constexpr double StaleResultsOpacity = 0.45;

    // The slot (top match or action card): its poster, 76 wide on a narrow
    // window, and the text beside it, which goes under the poster or glyph
    // when it would be narrower than 280.
    constexpr double SlotPosterWidth = 100.0;
    constexpr double SlotNarrowPosterWidth = 76.0;
    constexpr double SlotPosterAspect = 1.5;
    constexpr double SlotGap = 22.0;
    constexpr double SlotTextMinWidth = 280.0;
    // A slot narrower than this is on a window under about 640 wide.
    constexpr double SlotNarrowWidth = 592.0;
    // The empty page's hints: all in one row while each gets 170, else two to a row.
    constexpr double HintMinWidth = 170.0;
    constexpr double HintGap = 16.0;

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
        ::HaloDesktop::Controls::AttachSmoothWheelScroll(SearchScroller());
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
            LibraryGrid().ItemsSource(viewModel->LibraryItemsView());
            ResultsGrid().ItemsSource(viewModel->ResultItemsView());
            // One row of people, shown in one of its two places at a time.
            PeopleAboveGrid().ItemsSource(viewModel->PeopleItemsView());
            PeopleBelowGrid().ItemsSource(viewModel->PeopleItemsView());
        }
        m_viewModelChanged = m_viewModel.PropertyChanged(winrt::auto_revoke, [weak = get_weak()](
            winrt::Windows::Foundation::IInspectable const&,
            Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& changed)
        {
            auto const self = weak.get();
            if (!self) return;
            auto const name = std::wstring_view{ changed.PropertyName() };
            if (name == L"ShowKindBadges")
            {
                self->ApplyKindBadges(self->LibraryGrid());
                self->ApplyKindBadges(self->ResultsGrid());
            }
            if (name == L"ResultsStale")
            {
                self->ApplyResultsStale();
            }
            if (name == L"ActionTone" || name == L"ActionGlyph" || name == L"StatusFailed")
            {
                self->ApplyVisualStates();
            }
            if (name == L"PeopleHintVisibility")
            {
                self->ArrangeHints();
            }
            self->UpdateSkeletonPulse();
            // A query's results settle in when they arrive, which is when its
            // placeholders go, never on each keystroke.
            auto const loading = self->ResultsSkeleton().Visibility() == Microsoft::UI::Xaml::Visibility::Visible;
            if (self->m_resultsLoading && !loading) ::HaloDesktop::Controls::PlayContentEntrance(self->ResultsGrid());
            self->m_resultsLoading = loading;
        });
        m_onScreen = true;
        m_viewModel.Shown();
        ApplyResultsStale();
        ApplyVisualStates();
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
        // A magnet download is followed only while this page is on screen.
        m_viewModel.Hidden();
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
    // given no width; its visual states only ever change its visibility. A
    // button does not clip its content, so at no width its glyph still drew
    // past it, over whatever sits at the box's right edge: it is also made
    // fully transparent, which those states never touch.
    void SearchPage::OnQueryBoxLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto const builtIn = FindNamedDescendant(QueryBox(), L"DeleteButton").try_as<Microsoft::UI::Xaml::FrameworkElement>())
        {
            builtIn.Width(0.0);
            builtIn.MinWidth(0.0);
            builtIn.Opacity(0.0);
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

    void SearchPage::OnPersonClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto const button = sender.try_as<Microsoft::UI::Xaml::FrameworkElement>())
        {
            m_viewModel.OpenPerson(button.Tag());
        }
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
        auto const width = args.NewSize().Width;
        auto const posterWidth = width < SlotNarrowWidth ? SlotNarrowPosterWidth : SlotPosterWidth;
        if (TopMatchPosterColumn().Width().Value != posterWidth)
        {
            TopMatchPosterColumn().Width(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(posterWidth));
            TopMatchPoster().Height(posterWidth * SlotPosterAspect);
        }
        ArrangeSlot(TopMatchContent(), TopMatchPoster(), TopMatchText(), posterWidth, width);
    }

    void SearchPage::OnTopMatchOpenClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenTopMatch(); }

    // The action card acts as its one button does; its button acts for itself.
    void SearchPage::OnActionCardTapped(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args)
    {
        if (FromButton(args.OriginalSource(), ActionCard()))
        {
            return;
        }
        m_viewModel.ActivateAction();
    }

    void SearchPage::OnActionCardSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        m_actionCardWidth = args.NewSize().Width;
        ArrangeActionCard();
    }

    void SearchPage::ArrangeActionCard()
    {
        auto const card = ActionCard();
        auto const border = card.BorderThickness();
        auto const width = m_actionCardWidth - border.Left - border.Right;
        if (width <= 0.0) return;
        auto const posterWidth = width < SlotNarrowWidth ? SlotNarrowPosterWidth : SlotPosterWidth;
        auto const frame = ActionPosterFrame();
        if (frame.Width() != posterWidth)
        {
            frame.Width(posterWidth);
            frame.Height(posterWidth * SlotPosterAspect);
        }
        // The visual is the poster or the glyph tile, whichever the card leads with.
        auto const visualWidth = m_viewModel.ActionGlyph() == winrt::HaloDesktop::SearchActionGlyph::Poster ? posterWidth : ActionTile().Width();
        ArrangeSlot(card, ActionVisual(), ActionText(), visualWidth, width);
    }

    void SearchPage::OnActionButtonClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.ActivateAction(); }
    void SearchPage::OnActionPlainButtonClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.ActivatePlainAction(); }
    void SearchPage::OnTryAgainClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.RetrySearch(); }
    void SearchPage::OnOpenSettingsClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenSettings(); }

    void SearchPage::ArrangeSlot(
        Microsoft::UI::Xaml::Controls::Grid const& content,
        Microsoft::UI::Xaml::FrameworkElement const& visual,
        Microsoft::UI::Xaml::FrameworkElement const& text,
        double visualWidth,
        double contentWidth)
    {
        using Microsoft::UI::Xaml::Controls::Grid;
        auto const padding = content.Padding();
        auto const inner = contentWidth - padding.Left - padding.Right;
        auto const stacked = inner < visualWidth + SlotGap + SlotTextMinWidth;
        // Only what changed is set, as this runs on every size change.
        auto const row = stacked ? 1 : 0;
        auto const column = stacked ? 0 : 1;
        auto const span = stacked ? 2 : 1;
        if (Grid::GetRow(text) != row) Grid::SetRow(text, row);
        if (Grid::GetColumn(text) != column) Grid::SetColumn(text, column);
        if (Grid::GetColumnSpan(text) != span) Grid::SetColumnSpan(text, span);
        auto const columnSpacing = stacked ? 0.0 : SlotGap;
        auto const rowSpacing = stacked ? SlotGap : 0.0;
        if (content.ColumnSpacing() != columnSpacing) content.ColumnSpacing(columnSpacing);
        if (content.RowSpacing() != rowSpacing) content.RowSpacing(rowSpacing);
        visual.HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Left);
    }

    // The suggestions hang from the box at its own width.
    void SearchPage::OnQueryFieldSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        SuggestionPanel().Width(args.NewSize().Width);
    }

    void SearchPage::OnHintsGridSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        m_hintsWidth = args.NewSize().Width;
        ArrangeHints();
    }

    // The shown hints in legend order, all in one row when each gets its
    // minimum width, else two to a row. Columns are removed rather than left
    // empty, so their spacing does not push the row off centre.
    void SearchPage::ArrangeHints()
    {
        using Microsoft::UI::Xaml::Controls::Grid;
        std::vector<Microsoft::UI::Xaml::FrameworkElement> shown;
        for (auto const& hint : { HintTitles(), HintPeople(), HintLinks(), HintIds() })
        {
            if (hint.Visibility() == Microsoft::UI::Xaml::Visibility::Visible) shown.push_back(hint);
        }
        if (shown.empty()) return;
        auto const count = static_cast<double>(shown.size());
        auto const oneRow = m_hintsWidth >= count * HintMinWidth + (count - 1.0) * HintGap;
        auto const columnCount = static_cast<std::uint32_t>(oneRow ? shown.size() : (std::min)(shown.size(), std::size_t{ 2 }));
        auto const columns = HintsGrid().ColumnDefinitions();
        while (columns.Size() < columnCount)
        {
            Microsoft::UI::Xaml::Controls::ColumnDefinition column;
            column.Width(Microsoft::UI::Xaml::GridLengthHelper::FromValueAndType(1.0, Microsoft::UI::Xaml::GridUnitType::Star));
            columns.Append(column);
        }
        while (columns.Size() > columnCount)
        {
            columns.RemoveAtEnd();
        }
        for (std::uint32_t index = 0; index < shown.size(); ++index)
        {
            auto const row = static_cast<std::int32_t>(index / columnCount);
            auto const column = static_cast<std::int32_t>(index % columnCount);
            if (Grid::GetRow(shown[index]) != row) Grid::SetRow(shown[index], row);
            if (Grid::GetColumn(shown[index]) != column) Grid::SetColumn(shown[index], column);
        }
    }

    void SearchPage::ApplyVisualStates()
    {
        using Microsoft::UI::Xaml::VisualStateManager;
        wchar_t const* tone = L"ToneAccent";
        switch (m_viewModel.ActionTone())
        {
        case winrt::HaloDesktop::SearchActionTone::Accent: tone = L"ToneAccent"; break;
        case winrt::HaloDesktop::SearchActionTone::Warn: tone = L"ToneWarn"; break;
        case winrt::HaloDesktop::SearchActionTone::Bad: tone = L"ToneBad"; break;
        case winrt::HaloDesktop::SearchActionTone::Mute: tone = L"ToneMute"; break;
        }
        wchar_t const* glyph = L"GlyphId";
        switch (m_viewModel.ActionGlyph())
        {
        case winrt::HaloDesktop::SearchActionGlyph::Poster: glyph = L"GlyphPoster"; break;
        case winrt::HaloDesktop::SearchActionGlyph::Id: glyph = L"GlyphId"; break;
        case winrt::HaloDesktop::SearchActionGlyph::Link: glyph = L"GlyphLink"; break;
        case winrt::HaloDesktop::SearchActionGlyph::Magnet: glyph = L"GlyphMagnet"; break;
        }
        VisualStateManager::GoToState(*this, tone, false);
        VisualStateManager::GoToState(*this, glyph, false);
        VisualStateManager::GoToState(*this, m_viewModel.StatusFailed() ? L"StatusFailure" : L"StatusNormal", false);
        // A poster and a glyph tile differ in width, so whether the text fits beside it can change.
        ArrangeActionCard();
    }

    void SearchPage::ApplyLayoutMetrics()
    {
        auto const metrics = App::Services().LayoutMetrics;
        if (!metrics) return;
        auto const current = metrics->Current();
        PageBody().Padding({ current.Gutter, 40.0, current.Gutter, 40.0 });
        for (auto const& layout : { LibraryLayout(), ResultsLayout() })
        {
            if (!layout) continue;
            layout.MinItemWidth(current.PosterWidth);
            layout.MinItemHeight(current.PosterArtHeight() + CaptionHeight + LiftBand);
        }
    }

    void SearchPage::ApplyResultsStale()
    {
        ResultsBlock().Opacity(m_viewModel.ResultsStale() ? StaleResultsOpacity : 1.0);
    }

    void SearchPage::EnsureSkeletonPulse()
    {
        if (m_skeletonPulse) return;
        auto const compositor = Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(ResultsSkeletonGrid()).Compositor();
        m_skeletonPulse = std::make_unique<::HaloDesktop::Controls::SkeletonPulse>(compositor);
        m_skeletonPulse->Attach(TopMatchSkeleton());
        m_skeletonPulse->Attach(ResultsSkeletonGrid());
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
