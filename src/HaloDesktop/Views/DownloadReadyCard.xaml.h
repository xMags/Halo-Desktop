#pragma once

#include "DownloadReadyCard.g.h"

#include "Controls/ScrollHoverGate.h"

#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

#include <cstdint>
#include <winrt/Windows.Web.Http.h>

namespace winrt::HaloDesktop::implementation
{
    // One finished download in the "Ready to watch" grid. View behaviour only:
    // the 16:9 art at the column's width, the hover (art scale, frame, play
    // button) and the clicks handed to the page.
    struct DownloadReadyCard : DownloadReadyCardT<DownloadReadyCard>
    {
        DownloadReadyCard();

        [[nodiscard]] winrt::HaloDesktop::DownloadRowViewModel Row() const { return m_row; }
        void Row(winrt::HaloDesktop::DownloadRowViewModel const& value);

        winrt::event_token Selected(Microsoft::UI::Xaml::RoutedEventHandler const& handler) { return m_selectedEvent.add(handler); }
        void Selected(winrt::event_token const& token) noexcept { m_selectedEvent.remove(token); }
        winrt::event_token PlayRequested(Microsoft::UI::Xaml::RoutedEventHandler const& handler) { return m_playEvent.add(handler); }
        void PlayRequested(winrt::event_token const& token) noexcept { m_playEvent.remove(token); }

        void OnCardClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPlayClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPlayFocusChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnHoverStarted(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnHoverEnded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);


    private:
        void SetHovered(bool hovered);
        void UpdatePlayButton();
        void UpdatePlayName();
        // Starts over on the row's logo: the saved one, then the network's,
        // judged and, for a dark mark, inked light (Views/CardLogoLoader).
        winrt::fire_and_forget ShowLogo();
        // The text title back and the logo and its fade gone.
        void HideLogo();

        winrt::HaloDesktop::DownloadRowViewModel m_row{ nullptr };
        winrt::event_token m_rowChangedToken{};
        bool m_hovered{};
        bool m_playFocused{};
        // Advances with each logo asked for, so a slower earlier one never
        // replaces a later one on a recycled card.
        std::uint64_t m_logoRequest{};
        winrt::Windows::Web::Http::HttpClient m_logoClient{ nullptr };
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_selectedEvent;
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_playEvent;
        // Holds the hover back while the list under the card scrolls.
        ::HaloDesktop::Controls::ScrollHoverGate m_hoverGate;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct DownloadReadyCard : DownloadReadyCardT<DownloadReadyCard, implementation::DownloadReadyCard>
    {
    };
}
