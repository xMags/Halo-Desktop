#pragma once

#include "MediaShelf.g.h"

#include <winrt/Windows.Foundation.h>
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

        winrt::event_token ItemClick(Microsoft::UI::Xaml::RoutedEventHandler const& handler);
        void ItemClick(winrt::event_token const& token) noexcept;

        void OnPosterClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnScrollLeft(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnScrollRight(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void ScrollBy(double direction);

        winrt::hstring m_title;
        winrt::hstring m_sourceLabel;
        winrt::Windows::Foundation::IInspectable m_itemsSource{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_bindableItems{ nullptr };
        winrt::Windows::Foundation::IInspectable m_selectedItem{ nullptr };
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_itemClick;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct MediaShelf : MediaShelfT<MediaShelf, implementation::MediaShelf> {};
}
