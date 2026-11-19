#include "pch.h"
#include "Controls/MediaShelf.xaml.h"
#if __has_include("MediaShelf.g.cpp")
#include "MediaShelf.g.cpp"
#endif

#include "App.xaml.h"
#include "Controls/PosterCard.xaml.h"
#include "Controls/PosterSkeleton.xaml.h"
#include "Controls/SkeletonPulse.h"
#include "Controls/ShelfScrollPolicy.h"
#include "Controls/SidewaysWheelGuard.h"
#include "Shell/LayoutMetricsService.h"
#include <winrt/HaloDesktop.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Windows.System.h>

namespace winrt::HaloDesktop::implementation
{
    MediaShelf::MediaShelf() = default;
    winrt::hstring MediaShelf::Title() const { return m_title; }
    void MediaShelf::Title(winrt::hstring const& value) { m_title = value; if (auto text = FindName(L"TitleText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>()) text.Text(value); }
    winrt::hstring MediaShelf::SourceLabel() const { return m_sourceLabel; }
    void MediaShelf::SourceLabel(winrt::hstring const& value) { m_sourceLabel = value; if (auto text = FindName(L"SourceText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>()) text.Text(value); }
    winrt::Windows::Foundation::IInspectable MediaShelf::ItemsSource() const { return m_itemsSource; }
    void MediaShelf::ItemsSource(winrt::Windows::Foundation::IInspectable const& value)
    {
        m_itemsSource = value;
        if (auto items = FindName(L"ShelfItems").try_as<Microsoft::UI::Xaml::Controls::ItemsRepeater>())
        {
            m_bindableItems = winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>();
            if (value)
            {
                for (auto const& media : value.as<winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::MediaSummary>>())
                {
                    m_bindableItems.Append(media);
                }
            }
            items.ItemsSource(m_bindableItems);
        }
        // A recycled shelf is handed another row's items: it starts at their
        // first card, not where the last row was left.
        if (auto const scroller = ShelfScroller())
        {
            scroller.ChangeView(0.0, nullptr, nullptr, true);
        }
        UpdateEdges();
    }
    winrt::Windows::Foundation::IInspectable MediaShelf::SelectedItem() const { return m_selectedItem; }
    bool MediaShelf::IsLoading() const noexcept { return m_loading; }
    void MediaShelf::IsLoading(bool value)
    {
        m_loading = value;
        auto const shown = Microsoft::UI::Xaml::Visibility::Visible;
        auto const hidden = Microsoft::UI::Xaml::Visibility::Collapsed;
        if (auto const skeletons = SkeletonStrip())
        {
            skeletons.Visibility(value ? shown : hidden);
        }
        if (auto const scroller = ShelfScroller())
        {
            scroller.Visibility(value ? hidden : shown);
        }
        // Nothing to scroll yet: both chevrons inert, no fade. A recycled shelf
        // may have been somewhere else when it was handed this one.
        UpdateEdges();
    }
    void MediaShelf::AttachSkeletonPulse(::HaloDesktop::Controls::SkeletonPulse const& pulse)
    {
        if (m_pulseAttached)
        {
            return;
        }
        m_pulseAttached = true;
        pulse.Attach(SkeletonStrip());
    }
    winrt::event_token MediaShelf::ItemClick(Microsoft::UI::Xaml::RoutedEventHandler const& handler) { return m_itemClick.add(handler); }
    void MediaShelf::ItemClick(winrt::event_token const& token) noexcept { m_itemClick.remove(token); }
    winrt::event_token MediaShelf::SeeAllClick(Microsoft::UI::Xaml::RoutedEventHandler const& handler) { return m_seeAllClick.add(handler); }
    void MediaShelf::SeeAllClick(winrt::event_token const& token) noexcept { m_seeAllClick.remove(token); }
    void MediaShelf::OnPosterClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_selectedItem = sender.as<winrt::HaloDesktop::PosterCard>().Tag();
        m_itemClick(*this, args);
    }
    void MediaShelf::OnSeeAllClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_seeAllClick(*this, args);
    }
    void MediaShelf::OnScrollLeft([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { ScrollBy(-1); }
    void MediaShelf::OnScrollRight([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { ScrollBy(1); }

    void MediaShelf::OnStripPointerWheelChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        ::HaloDesktop::Controls::GuardSidewaysWheel(args);
    }

    void MediaShelf::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const metrics = App::Services().LayoutMetrics;
        if (metrics && m_metricsToken == 0)
        {
            m_metricsToken = metrics->AddChangedHandler([weak = get_weak()]()
            {
                if (auto const self = weak.get())
                {
                    self->ApplyEndInset();
                }
            });
        }
        ApplyEndInset();
    }

    void MediaShelf::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        // A shelf moved within the tree can be loaded again before the old
        // place's Unloaded arrives: it is still listening then.
        if (IsLoaded() || m_metricsToken == 0)
        {
            return;
        }
        if (auto const metrics = App::Services().LayoutMetrics)
        {
            metrics->RemoveChangedHandler(m_metricsToken);
        }
        m_metricsToken = 0;
    }

    void MediaShelf::ApplyEndInset() noexcept
    {
        try
        {
            auto const metrics = App::Services().LayoutMetrics;
            auto const inset = metrics ? metrics->Current().Gutter : 24.0;
            if (inset == m_endInset)
            {
                return;
            }
            m_endInset = inset;
            HeaderRow().Margin(Microsoft::UI::Xaml::Thickness{ 0.0, 0.0, inset, 12.0 });
            ShelfItems().Margin(Microsoft::UI::Xaml::Thickness{ 0.0, 0.0, inset, 0.0 });
            UpdateEdges();
        }
        catch (winrt::hresult_error const&)
        {
            // A shelf leaving the tree: the next load sets it again.
        }
    }

    // Pages by the whole cards that fit, landing on a card boundary, so a click
    // never leaves a half card at either edge. Every card on a shelf is a
    // poster, as wide as the layout step makes posters.
    void MediaShelf::ScrollBy(int direction)
    {
        auto const scroller = ShelfScroller();
        auto const metrics = App::Services().LayoutMetrics;
        auto const cardWidth = metrics ? metrics->Current().PosterWidth : 132.0;
        auto const target = ::HaloDesktop::Controls::ShelfPageTarget(
            scroller.HorizontalOffset(),
            scroller.ViewportWidth(),
            cardWidth,
            ::HaloDesktop::Controls::ShelfCardGap,
            direction);
        scroller.ChangeView(target, nullptr, nullptr, false);
    }

    void MediaShelf::OnStripSizeChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        UpdateEdges();
    }

    void MediaShelf::OnStripViewChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const& args)
    {
        UpdateEdges();
    }

    // The chevrons are always drawn rather than appearing on demand: a control
    // that comes and goes as a catalog resolves makes the row jitter. The one
    // pointing at an end already in view is drawn but plainly inert instead.
    void MediaShelf::UpdateEdges()
    {
        auto const scroller = ShelfScroller();
        if (!scroller) return;
        auto const edges = m_loading
            ? ::HaloDesktop::Controls::ShelfEdges{}
            : ::HaloDesktop::Controls::MeasureShelfEdges(
                scroller.HorizontalOffset(),
                scroller.ViewportWidth(),
                scroller.ExtentWidth());
        auto const apply = [](Microsoft::UI::Xaml::Controls::Button const& button, bool atEnd)
        {
            if (!button) return;
            button.IsEnabled(!atEnd);
            button.Opacity(atEnd ? 0.4 : 1.0);
        };
        apply(ScrollLeftButton(), edges.AtStart);
        apply(ScrollRightButton(), edges.AtEnd);
        // The start fade marks that cards are scrolled past, so it follows the
        // same end the left chevron does.
        if (auto const fade = StartFade())
        {
            fade.Visibility(edges.AtStart ? Microsoft::UI::Xaml::Visibility::Collapsed : Microsoft::UI::Xaml::Visibility::Visible);
        }
    }
}
