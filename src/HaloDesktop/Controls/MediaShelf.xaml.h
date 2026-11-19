#pragma once

#include "MediaShelf.g.h"

#include "Shell/LayoutMetricsService.h"

#include <winrt/Windows.Foundation.h>

namespace HaloDesktop::Controls
{
    class SkeletonPulse;
}
#include <winrt/Windows.Foundation.Collections.h>

namespace winrt::HaloDesktop::implementation
{
    struct MediaShelf : MediaShelfT<MediaShelf>
    {
        MediaShelf();

        [[nodiscard]] winrt::hstring Title() const;
        void Title(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring SourceLabel() const;
        void SourceLabel(winrt::hstring const& value);
        [[nodiscard]] winrt::Windows::Foundation::IInspectable ItemsSource() const;
        void ItemsSource(winrt::Windows::Foundation::IInspectable const& value);
        [[nodiscard]] winrt::Windows::Foundation::IInspectable SelectedItem() const;
        [[nodiscard]] bool IsLoading() const noexcept;
        void IsLoading(bool value);
        // Not projected. The page's one pulse drives this shelf's skeletons.
        void AttachSkeletonPulse(::HaloDesktop::Controls::SkeletonPulse const& pulse);

        winrt::event_token ItemClick(Microsoft::UI::Xaml::RoutedEventHandler const& handler);
        void ItemClick(winrt::event_token const& token) noexcept;
        winrt::event_token SeeAllClick(Microsoft::UI::Xaml::RoutedEventHandler const& handler);
        void SeeAllClick(winrt::event_token const& token) noexcept;

        void OnPosterClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSeeAllClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnScrollLeft(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnScrollRight(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        // The strips scroll by their own arrows only. A tilt wheel or a shift
        // wheel over one would otherwise pan it, which no other list here does.
        void OnStripPointerWheelChanged(
            winrt::Windows::Foundation::IInspectable const&,
            Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        // The strip's ends move with its own width (the window), its cards (a
        // catalog arriving) and its offset, so all three re-measure them.
        void OnStripSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnStripViewChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const&);
        // Follows the layout step while in the tree, for the right-edge gutter.
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void ScrollBy(int direction);
        void UpdateEdges();
        // The header and the strip's end keep the page gutter at the window's
        // right edge, which the shelf itself runs to.
        void ApplyEndInset() noexcept;

        winrt::hstring m_title;
        winrt::hstring m_sourceLabel;
        winrt::Windows::Foundation::IInspectable m_itemsSource{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_bindableItems{ nullptr };
        winrt::Windows::Foundation::IInspectable m_selectedItem{ nullptr };
        bool m_loading{};
        bool m_pulseAttached{};
        ::HaloDesktop::Shell::LayoutMetricsChangedToken m_metricsToken{};
        double m_endInset{ 24.0 };
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_itemClick;
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_seeAllClick;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct MediaShelf : MediaShelfT<MediaShelf, implementation::MediaShelf> {};
}
