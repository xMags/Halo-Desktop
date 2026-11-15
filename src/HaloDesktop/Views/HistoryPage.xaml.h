#pragma once

#include "HistoryPage.g.h"

#include "Views/TitleMenu.h"

#include <cstdint>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

namespace winrt::HaloDesktop::implementation
{
    // The History page's view: the header's controls, the grouped cards and
    // their hover-revealed buttons, the menus and the dialogs. What is shown and
    // every removal live in HistoryViewModel.
    struct HistoryPage : HistoryPageT<HistoryPage>
    {
        HistoryPage();
        [[nodiscard]] winrt::HaloDesktop::HistoryViewModel ViewModel() const;

        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnContextRequested(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const&);
        void OnQueryBoxLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnClearQueryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAllFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnMoviesFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSeriesFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnClearHistoryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnScrollerSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnEntryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPlayClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnMoreClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCardPrepared(Microsoft::UI::Xaml::Controls::ItemsRepeater const&, Microsoft::UI::Xaml::Controls::ItemsRepeaterElementPreparedEventArgs const&);
        void OnCardPointerEntered(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnCardPointerExited(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnCardGotFocus(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCardLostFocus(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void ApplyLayoutMetrics();
        void ApplyColumnWidth();
        // Shows a card's buttons and hover fill while the pointer is over it or
        // focus is inside it. Decorative: a refusal from XAML leaves it as it was.
        void UpdateCard(Microsoft::UI::Xaml::FrameworkElement const& card);
        void ShowMenu(
            Microsoft::UI::Xaml::FrameworkElement const& anchor,
            Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args,
            winrt::HaloDesktop::HistoryEntryViewModel const& item);
        winrt::fire_and_forget RemoveVideo(winrt::hstring videoId);
        winrt::fire_and_forget ClearHistory();

        winrt::HaloDesktop::HistoryViewModel m_viewModel{ nullptr };
        ::HaloDesktop::Views::TitleMenu m_titleMenu;
        // The one card under the pointer; only one can be.
        winrt::weak_ref<Microsoft::UI::Xaml::FrameworkElement> m_hoveredCard;
        std::uint64_t m_metricsToken{};
        double m_gutter{ 24.0 };
        bool m_listBound{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct HistoryPage : HistoryPageT<HistoryPage, implementation::HistoryPage>
    {
    };
}
