#pragma once

#include "DetailEpisodeRow.g.h"

#include <winrt/Microsoft.UI.Xaml.Input.h>

namespace winrt::HaloDesktop::implementation
{
    // One episode row of the title page. View behaviour only: the hover fill and
    // play circle, and the click handed to its host.
    struct DetailEpisodeRow : DetailEpisodeRowT<DetailEpisodeRow>
    {
        DetailEpisodeRow() = default;

        [[nodiscard]] winrt::HaloDesktop::DetailEpisodeViewModel Episode() const { return m_episode; }
        void Episode(winrt::HaloDesktop::DetailEpisodeViewModel const& value);

        winrt::event_token Click(Microsoft::UI::Xaml::RoutedEventHandler const& handler) { return m_click.add(handler); }
        void Click(winrt::event_token const& token) noexcept { m_click.remove(token); }

        void OnClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnHoverStarted(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnHoverEnded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);

    private:
        void SetHovered(bool hovered);

        winrt::HaloDesktop::DetailEpisodeViewModel m_episode{ nullptr };
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_click;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct DetailEpisodeRow : DetailEpisodeRowT<DetailEpisodeRow, implementation::DetailEpisodeRow>
    {
    };
}
