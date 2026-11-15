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

    // Only sideways intent is swallowed. A plain wheel still reaches the page
    // scroller above, so pointing at a strip does not trap the page.
    void MediaShelf::OnStripPointerWheelChanged(
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
        // The fade marks the side that still hides cards, so it follows the
        // same ends the chevrons do.
        auto const fade = [](Microsoft::UI::Xaml::UIElement const& element, bool atEnd)
        {
            if (!element) return;
            element.Visibility(atEnd ? Microsoft::UI::Xaml::Visibility::Collapsed : Microsoft::UI::Xaml::Visibility::Visible);
        };
        fade(StartFade(), edges.AtStart);
        fade(EndFade(), edges.AtEnd);
    }
}
