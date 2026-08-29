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
    // The FlipView flips cards by panning its internal ScrollViewer, and nothing
    // on the public surface disables that gesture. Take the manipulation away
    // from the template's scroll host: the auto-advance timer, the pips pager
    // and the arrow keys all move the selection directly, so none of them need
    // it. The host is named "ScrollingHost" in the WinUI 3.2.4.0 template; on a
    // future SDK bump that reaches the scroll host differently, this scan just
    // finds nothing and swiping silently comes back.
    void DisableFlipViewSwipe(winrt::Microsoft::UI::Xaml::DependencyObject const& node)
    {
        if (auto const element = node.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
        {
            if (element.Name() == L"ScrollingHost")
            {
                element.as<winrt::Microsoft::UI::Xaml::Controls::ScrollViewer>().ManipulationMode(
                    winrt::Microsoft::UI::Xaml::Input::ManipulationModes::None);
                return;
            }
        }
        auto const count = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChildrenCount(node);
        for (auto index = 0; index < count; ++index)
        {
            DisableFlipViewSwipe(winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChild(node, index));
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
        DisableFlipViewSwipe(
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
