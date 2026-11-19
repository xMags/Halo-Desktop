#pragma once

#include "DetailSimilarCard.g.h"

#include "Controls/ScrollHoverGate.h"

#include <winrt/Microsoft.UI.Xaml.Input.h>

namespace winrt::HaloDesktop::implementation
{
    // A title on the "More like this" row. View behaviour only.
    struct DetailSimilarCard : DetailSimilarCardT<DetailSimilarCard>
    {
        DetailSimilarCard();

        [[nodiscard]] winrt::HaloDesktop::MediaSummary Media() const { return m_media; }
        void Media(winrt::HaloDesktop::MediaSummary const& value);

        winrt::event_token Click(Microsoft::UI::Xaml::RoutedEventHandler const& handler) { return m_click.add(handler); }
        void Click(winrt::event_token const& token) noexcept { m_click.remove(token); }

        void OnClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnHoverStarted(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnHoverEnded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);

    private:
        void SetHovered(bool hovered);

        winrt::HaloDesktop::MediaSummary m_media{ nullptr };
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_click;
        // Holds the hover back while the list under the card scrolls.
        ::HaloDesktop::Controls::ScrollHoverGate m_hoverGate;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct DetailSimilarCard : DetailSimilarCardT<DetailSimilarCard, implementation::DetailSimilarCard>
    {
    };
}
