#include "pch.h"
#include "Views/HomePage.xaml.h"
#include "Controls/ContentEntrance.h"
#if __has_include("HomePage.g.cpp")
#include "HomePage.g.cpp"
#endif

#include "App.xaml.h"
#include "Controls/ArtworkImage.xaml.h"
#include "Controls/ContinueCard.xaml.h"
#include "Controls/MediaShelf.xaml.h"
#include "Controls/PlaceholderArt.xaml.h"
#include "Controls/PosterSkeleton.xaml.h"
#include "Controls/SearchBoxFlight.h"
#include "Controls/ShelfScrollPolicy.h"
#include "Controls/SidewaysWheelGuard.h"
#include "Controls/SmoothWheelScroll.h"
#include "Models/Models.h"
#include "Services/NavigationService.h"
#include "Shell/LayoutMetricsService.h"
#include "ViewModels/HomeViewModel.h"

#include <string_view>
#include <vector>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.System.h>

namespace
{
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

    bool IsHeroProperty(std::wstring_view name) noexcept
    {
        return name.empty() || name == L"FeaturedItems" || name == L"FeaturedIndex"
            || name == L"FeaturedAutoAdvance" || name == L"CurrentFeatured";
    }
}

namespace winrt::HaloDesktop::implementation
{
    HomePage::HomePage()
        : m_viewModel(winrt::make<HomeViewModel>(App::Services())),
          m_titleMenu(App::Services())
    {
    }

    void HomePage::OnContextRequested(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args)
    {
        m_titleMenu.ShowForSubject(args);
    }

    winrt::HaloDesktop::HomeViewModel HomePage::ViewModel() const
    {
        return m_viewModel;
    }

    void HomePage::EnsureHeroPresenter()
    {
        if (m_hero)
        {
            return;
        }
        ::HaloDesktop::Views::HomeHeroParts parts;
        parts.Root = HeroRoot();
        parts.ArtHost = HeroArtHost();
        parts.Placeholder = HeroPlaceholder();
        parts.Body = HeroBody();
        parts.Logo = HeroLogo();
        parts.LogoShadowHost = HeroLogoShadowHost();
        parts.LogoFrame = HeroLogoFrame();
        parts.TitleText = HeroTitleText();
        parts.PipStrip = HeroPips();
        parts.Pips = { HeroPip0(), HeroPip1(), HeroPip2(), HeroPip3(), HeroPip4() };
        parts.PipFills = { HeroPipFill0(), HeroPipFill1(), HeroPipFill2(), HeroPipFill3(), HeroPipFill4() };
        parts.PipScales = { HeroPipScale0(), HeroPipScale1(), HeroPipScale2(), HeroPipScale3(), HeroPipScale4() };
        parts.PipStyle = Resources().Lookup(winrt::box_value(L"HeroPipStyle")).as<Microsoft::UI::Xaml::Style>();
        parts.ActivePipStyle = Resources().Lookup(winrt::box_value(L"HeroActivePipStyle")).as<Microsoft::UI::Xaml::Style>();
        // Weak: the presenter is the page's, but the clock's completion is an
        // event the framework raises, and nothing here may reach a page that
        // has already gone.
        m_hero = std::make_unique<::HaloDesktop::Views::HomeHeroPresenter>(std::move(parts), [weak = get_weak()]()
        {
            if (auto const self = weak.get())
            {
                self->m_viewModel.AdvanceFeatured();
            }
        });

        m_viewModelChanged = m_viewModel.PropertyChanged(winrt::auto_revoke, [weak = get_weak()](
            winrt::Windows::Foundation::IInspectable const&,
            Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args)
        {
            auto const self = weak.get();
            if (!self)
            {
                return;
            }
            if (IsHeroProperty(args.PropertyName()))
            {
                self->SyncHero();
            }
            self->UpdateSkeletonPulse();
            ::HaloDesktop::Controls::PlayEntranceWhenShown(self->PageScroller(), self->m_contentShown);
        });
        m_themeChanged = ActualThemeChanged(winrt::auto_revoke, [weak = get_weak()](auto const&, auto const&)
        {
            if (auto const self = weak.get(); self && self->m_hero)
            {
                self->m_hero->OnThemeChanged();
            }
        });
    }

    void HomePage::EnsureSkeletonPulse()
    {
        if (m_skeletonPulse)
        {
            return;
        }
        auto const compositor = Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(HeroSkeleton()).Compositor();
        m_skeletonPulse = std::make_unique<::HaloDesktop::Controls::SkeletonPulse>(compositor);
        m_skeletonPulse->Attach(HeroSkeletonBody());
        m_skeletonPulse->Attach(ShelfSkeleton0());
        m_skeletonPulse->Attach(ShelfSkeleton1());
    }

    void HomePage::UpdateSkeletonPulse()
    {
        if (!m_skeletonPulse)
        {
            return;
        }
        m_skeletonPulse->SetActive(m_onScreen && winrt::get_self<HomeViewModel>(m_viewModel)->SkeletonsShown());
    }

    void HomePage::OnShelfElementPrepared(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::ItemsRepeater const& sender,
        Microsoft::UI::Xaml::Controls::ItemsRepeaterElementPreparedEventArgs const& args)
    {
        // The list is bound only after the pulse exists; this is a guard.
        if (!m_skeletonPulse)
        {
            return;
        }
        if (auto const shelf = args.Element().try_as<winrt::HaloDesktop::MediaShelf>())
        {
            winrt::get_self<MediaShelf>(shelf)->AttachSkeletonPulse(*m_skeletonPulse);
        }
    }

    void HomePage::SyncHero()
    {
        if (!m_hero)
        {
            return;
        }
        auto const viewModel = winrt::get_self<HomeViewModel>(m_viewModel);
        m_hero->Sync(viewModel->FeaturedItemsView(), m_viewModel.FeaturedIndex(), m_viewModel.FeaturedAutoAdvance());
    }

    void HomePage::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        // First, so the shelves the list realises can follow it.
        EnsureSkeletonPulse();
        // Bound once for the life of the page. Handing a repeater the same
        // collection again resets it, and this runs on every return to Home.
        if (!m_listsBound)
        {
            m_listsBound = true;
            auto const viewModel = winrt::get_self<HomeViewModel>(m_viewModel);
            ContinueList().ItemsSource(viewModel->ContinueItemsView());
            ShelfList().ItemsSource(viewModel->ShelvesView());
            ::HaloDesktop::Controls::AttachSmoothWheelScroll(PageScroller());
            if (auto const metrics = App::Services().LayoutMetrics)
            {
                try
                {
                    m_continueFit = std::make_unique<::HaloDesktop::Controls::WholeCardStrip>(
                        ContinueScroller(),
                        ContinueList(),
                        metrics,
                        &::HaloDesktop::Shell::LayoutMetrics::ContinueWidth,
                        ::HaloDesktop::Controls::ShelfCardGap);
                }
                catch (...)
                {
                    // Without it the cards keep the step's continue width, as before.
                }
            }
        }
        EnsureHeroPresenter();
        m_viewModel.EnsureLoaded();
        SyncHero();
        // The theme may have changed while Home was cached off screen.
        m_hero->OnThemeChanged();
        m_hero->SetRunning(true);
        m_onScreen = true;
        UpdateSkeletonPulse();
        UpdateContinueEdges();
    }

    void HomePage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        // Stop the carousel while Home is off screen; its dwell starts over on
        // the next OnLoaded.
        m_viewModel.Deactivate();
        if (m_hero)
        {
            m_hero->SetRunning(false);
        }
        m_onScreen = false;
        UpdateSkeletonPulse();
    }

    void HomePage::OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args)
    {
        if (args.NavigationMode() != Microsoft::UI::Xaml::Navigation::NavigationMode::Back) return;
        // None prepared (Back from anywhere but a Search the box opened) is no flight.
        [[maybe_unused]] auto const started = ::HaloDesktop::Controls::StartSearchBoxReturn(HomeSearchBox());
    }

    void HomePage::OnSearchClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        ::HaloDesktop::Controls::PrepareSearchBoxFlight(HomeSearchBox());
        m_viewModel.OpenSearch();
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

    // The hero's buttons hand the current FeaturedItem through Tag, so a click
    // acts on the title the button was showing.
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
        if (m_hero) m_hero->SetHovered(true);
    }

    void HomePage::OnFeaturedPointerExited(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (m_hero) m_hero->SetHovered(false);
    }

    void HomePage::OnHeroPipClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const button = sender.try_as<Microsoft::UI::Xaml::Controls::Button>();
        if (!button)
        {
            return;
        }
        auto const tag = winrt::unbox_value_or<winrt::hstring>(button.Tag(), L"");
        if (tag.size() != 1 || tag[0] < L'0' || tag[0] > L'9')
        {
            return;
        }
        m_viewModel.SelectFeatured(static_cast<std::int32_t>(tag[0] - L'0'));
    }

    // Left and Right step between the titles while focus is on a pip.
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
        ScrollContinueBy(-1);
    }

    void HomePage::OnContinueScrollRight(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        ScrollContinueBy(1);
    }

    // The continue strip pages the way every shelf does: by the whole cards
    // in view, landing on a card boundary.
    void HomePage::ScrollContinueBy(int direction)
    {
        auto const scroller = ContinueScroller();
        auto const metrics = App::Services().LayoutMetrics;
        auto const cardWidth = m_continueFit ? m_continueFit->CardWidth() : metrics ? metrics->Current().ContinueWidth : 268.0;
        auto const target = ::HaloDesktop::Controls::ShelfPageTarget(
            scroller.HorizontalOffset(),
            scroller.ViewportWidth(),
            cardWidth,
            ::HaloDesktop::Controls::ShelfCardGap,
            direction);
        scroller.ChangeView(target, nullptr, nullptr, false);
    }

    void HomePage::OnContinueViewChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const& args)
    {
        UpdateContinueEdges();
    }

    void HomePage::OnContinueScrollerSizeChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        UpdateContinueEdges();
    }

    // The chevron toward an end already in view is drawn but plainly inert; a
    // row whose cards all fit draws none (ShelfArrowsShown).
    void HomePage::UpdateContinueEdges()
    {
        auto const scroller = ContinueScroller();
        auto const arrows = ::HaloDesktop::Controls::ShelfArrowsShown(false, scroller.ViewportWidth(), scroller.ExtentWidth())
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed;
        ContinueScrollLeftButton().Visibility(arrows);
        ContinueScrollRightButton().Visibility(arrows);
        auto const edges = ::HaloDesktop::Controls::MeasureShelfEdges(
            scroller.HorizontalOffset(),
            scroller.ViewportWidth(),
            scroller.ExtentWidth());
        auto const apply = [](Microsoft::UI::Xaml::Controls::Button const& button, bool atEnd)
        {
            button.IsEnabled(!atEnd);
            button.Opacity(atEnd ? 0.4 : 1.0);
        };
        apply(ContinueScrollLeftButton(), edges.AtStart);
        apply(ContinueScrollRightButton(), edges.AtEnd);
    }

    void HomePage::OnShelfItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const shelf = sender.as<winrt::HaloDesktop::MediaShelf>();
        m_viewModel.OpenDetail(shelf.SelectedItem().as<winrt::HaloDesktop::MediaSummary>());
    }

    void HomePage::OnStripPointerWheelChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        ::HaloDesktop::Controls::GuardSidewaysWheel(args);
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
        // The shelf the strip was made from knows where its See all leads and
        // how its catalog pages; a strip without one opens what it shows.
        if (auto const model = shelf.Tag().try_as<winrt::HaloDesktop::Shelf>())
        {
            m_viewModel.OpenCatalog(model);
            return;
        }
        m_viewModel.OpenCatalog(CreateShelfSnapshot(shelf));
    }
}
