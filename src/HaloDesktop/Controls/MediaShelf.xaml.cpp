#include "pch.h"
#include "Controls/MediaShelf.xaml.h"
#if __has_include("MediaShelf.g.cpp")
#include "MediaShelf.g.cpp"
#endif

#include "Controls/PosterCard.xaml.h"
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
    }
    winrt::Windows::Foundation::IInspectable MediaShelf::SelectedItem() const { return m_selectedItem; }
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
    void MediaShelf::OnScrollLeft([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { ScrollBy(-1.0); }
    void MediaShelf::OnScrollRight([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { ScrollBy(1.0); }

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

    void MediaShelf::ScrollBy(double direction)
    {
        auto const scroller = FindName(L"ShelfScroller").as<Microsoft::UI::Xaml::Controls::ScrollViewer>();
        scroller.ChangeView(scroller.HorizontalOffset() + direction * scroller.ViewportWidth() * 0.8, nullptr, nullptr, false);
    }
}
