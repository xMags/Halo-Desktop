#include "pch.h"
#include "Views/HomePage.xaml.h"
#if __has_include("HomePage.g.cpp")
#include "HomePage.g.cpp"
#endif

#include "App.xaml.h"
#include "Controls/ContinueCard.xaml.h"
#include "Controls/MediaShelf.xaml.h"
#include "Models/Models.h"
#include "Services/NavigationService.h"
#include "ViewModels/HomeViewModel.h"

#include <vector>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Windows.System.h>

namespace
{
    // One wheel notch, in pixels. Matches what the page's own ScrollViewer moves
    // for a notch, measured against it, so a scroll that begins over the hero and
    // continues below it keeps one rate.
    constexpr double HeroWheelStep = 150.0;

    // A FlipView carries a horizontally scrollable region, and Windows redirects a
    // plain vertical wheel into whichever axis a scroller can move. Over the hero
    // that turned an ordinary scroll-down into a sideways flip through the
    // featured cards, and the page below only began to move once the strip ran
    // out of cards to flip. It also flips by panning that same scroller, which
    // nothing on the public surface disables.
    //
    // So the scroll host is stripped of both: no manipulation, and no scrollable
    // axis for the wheel to be redirected into. Selection is unaffected because
    // nothing here moves it by scrolling. The auto-advance timer, the pips pager
    // and the arrow keys all set SelectedIndex directly.
    //
    // The stock previous/next chevrons go at the same time. They are the same
    // control surfacing the same sideways model, they are drawn in the system's
    // acrylic rather than anything of Halo's, and the pips pager underneath
    // already offers the position and the jumps.
    //
    // These are template part names from the WinUI 3.2.4.0 FlipView. On a future
    // SDK bump that names them differently this scan simply finds nothing, and
    // the stock behaviour quietly returns rather than anything breaking.
    void TameFlipViewChrome(winrt::Microsoft::UI::Xaml::DependencyObject const& node)
    {
        if (auto const element = node.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
        {
            auto const name = element.Name();
            if (name == L"ScrollingHost")
            {
                auto const host = element.as<winrt::Microsoft::UI::Xaml::Controls::ScrollViewer>();
                host.ManipulationMode(winrt::Microsoft::UI::Xaml::Input::ManipulationModes::None);
                host.HorizontalScrollMode(winrt::Microsoft::UI::Xaml::Controls::ScrollMode::Disabled);
                host.VerticalScrollMode(winrt::Microsoft::UI::Xaml::Controls::ScrollMode::Disabled);
                host.IsHorizontalScrollChainingEnabled(false);
                host.IsVerticalScrollChainingEnabled(false);
                // Not descended into: the cards below it are the page's whole
                // featured list, and nothing under here needs touching.
                return;
            }
            if (name == L"PreviousButtonHorizontal" || name == L"NextButtonHorizontal" ||
                name == L"PreviousButtonVertical" || name == L"NextButtonVertical")
            {
                element.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
                return;
            }
        }
        auto const count = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChildrenCount(node);
        for (auto index = 0; index < count; ++index)
        {
            TameFlipViewChrome(winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChild(node, index));
        }
    }

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
        // Bound once for the life of the page. Handing a repeater the same
        // collection again resets it, and this runs on every return to Home.
        if (!m_listsBound)
        {
            m_listsBound = true;
            auto const viewModel = winrt::get_self<HomeViewModel>(m_viewModel);
            FindName(L"ContinueList")
                .as<Microsoft::UI::Xaml::Controls::ItemsRepeater>()
                .ItemsSource(viewModel->ContinueItemsView());
            FindName(L"ShelfList")
                .as<Microsoft::UI::Xaml::Controls::ItemsRepeater>()
                .ItemsSource(viewModel->ShelvesView());
            FindName(L"FeaturedFlip")
                .as<Microsoft::UI::Xaml::Controls::FlipView>()
                .ItemsSource(viewModel->FeaturedItemsView());
        }
        TameFlipViewChrome(
            FindName(L"FeaturedFlip")
                .as<winrt::Microsoft::UI::Xaml::DependencyObject>());
        m_viewModel.EnsureLoaded();
    }

    void HomePage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        // Stop the carousel while Home is off screen; it restarts on the next
        // OnLoaded through EnsureLoaded.
        m_viewModel.Deactivate();
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

    // The card buttons hand their FeaturedItem through Tag, the same trick
    // ContinueCard uses, because a DataTemplate has no other way to say which
    // row a click came from.
    void HomePage::OnFeaturedActionClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        if (auto const button = sender.try_as<Microsoft::UI::Xaml::Controls::Button>())
        {
            m_viewModel.OpenFeaturedSources(button.Tag());
        }
    }

    void HomePage::OnFeaturedDetailsClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        if (auto const button = sender.try_as<Microsoft::UI::Xaml::Controls::Button>())
        {
            m_viewModel.OpenFeaturedDetail(button.Tag());
        }
    }

    void HomePage::OnFeaturedLibraryClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        if (auto const button = sender.try_as<Microsoft::UI::Xaml::Controls::Button>())
        {
            m_viewModel.ToggleFeaturedLibrary(button.Tag());
        }
    }

    void HomePage::OnFeaturedPointerEntered(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        m_viewModel.PauseFeatured();
    }

    void HomePage::OnFeaturedPointerExited(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        m_viewModel.ResumeFeatured();
    }

    // Left and Right flip the strip while focus is anywhere on it. The FlipView
    // is not a tab stop by default, so its own key handling would not see these.
    void HomePage::OnFeaturedKeyDown(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        switch (args.Key())
        {
        case winrt::Windows::System::VirtualKey::Left:
            args.Handled(true);
            m_viewModel.StepFeatured(-1);
            break;
        case winrt::Windows::System::VirtualKey::Right:
            args.Handled(true);
            m_viewModel.StepFeatured(1);
            break;
        default:
            break;
        }
    }

    void HomePage::OnContinueItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const card = sender.try_as<winrt::HaloDesktop::ContinueCard>();
        if (!card) return;
        m_viewModel.OpenContinue(card.Tag());
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

    // Only sideways intent is swallowed. A plain wheel still reaches the page
    // scroller above, so pointing at a strip does not trap the page.
    void HomePage::OnStripPointerWheelChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto const point = args.GetCurrentPoint(nullptr);
        auto const shifted =
            (args.KeyModifiers() & winrt::Windows::System::VirtualKeyModifiers::Shift) !=
            winrt::Windows::System::VirtualKeyModifiers::None;
        if (point && (point.Properties().IsHorizontalMouseWheel() || shifted))
        {
            args.Handled(true);
        }
    }

    // FlipView treats a wheel notch as "flip to the next card", and it does that
    // from its own control-level handler, so neither disabling its scroll host nor
    // attaching anything at or above the FlipView prevents it. The card content is
    // below the FlipView in the bubble order, so taking the event here is the only
    // point that runs first.
    //
    // Having taken it, this has to move the page itself: the event is marked
    // handled, so nothing above will. Horizontal intent is swallowed outright, the
    // way the shelves treat it.
    void HomePage::OnHeroPointerWheelChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto const point = args.GetCurrentPoint(nullptr);
        if (!point)
        {
            return;
        }

        auto const properties = point.Properties();
        args.Handled(true);
        if (properties.IsHorizontalMouseWheel())
        {
            return;
        }

        auto const scroller = FindName(L"PageScroller")
            .try_as<Microsoft::UI::Xaml::Controls::ScrollViewer>();
        if (!scroller)
        {
            return;
        }

        // Read the offset fresh each notch rather than accumulating, so a fast
        // spin retargets from where the view actually is instead of from where
        // the previous notch aimed.
        auto const notches = static_cast<double>(properties.MouseWheelDelta()) / 120.0;
        scroller.ChangeView(nullptr, scroller.VerticalOffset() - notches * HeroWheelStep, nullptr, false);
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
