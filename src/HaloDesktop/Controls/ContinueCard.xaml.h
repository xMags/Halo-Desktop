#pragma once

#include "ContinueCard.g.h"

#include <cstdint>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    // The in-progress twin of PosterCard: 16:9 still instead of 2:3 artwork, and
    // it sizes itself from the layout step for the same reason PosterCard does.
    struct ContinueCard : ContinueCardT<ContinueCard>
    {
        ContinueCard();

        [[nodiscard]] winrt::hstring Title() const;
        void Title(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring Meta() const;
        void Meta(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring Art() const;
        void Art(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring EpisodeTag() const;
        void EpisodeTag(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring TimeLeft() const;
        void TimeLeft(winrt::hstring const& value);
        [[nodiscard]] double Progress() const noexcept;
        void Progress(double value);
        [[nodiscard]] double CardWidth() const noexcept;

        winrt::event_token Click(Microsoft::UI::Xaml::RoutedEventHandler const& handler);
        void Click(winrt::event_token const& token) noexcept;

        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCardClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCardPointerEntered(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnCardPointerExited(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);

    private:
        void ApplyLayoutMetrics();
        void ApplyProgress();
        void SetHovered(bool hovered);

        winrt::hstring m_title;
        winrt::hstring m_meta;
        winrt::hstring m_art;
        winrt::hstring m_episodeTag;
        winrt::hstring m_timeLeft;
        double m_progress{};
        double m_cardWidth{ 268.0 };
        std::uint64_t m_metricsToken{};
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_click;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct ContinueCard : ContinueCardT<ContinueCard, implementation::ContinueCard> {};
}
