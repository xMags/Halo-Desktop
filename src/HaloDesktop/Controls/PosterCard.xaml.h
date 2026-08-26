#pragma once

#include "PosterCard.g.h"

#include <cstdint>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct PosterCard : PosterCardT<PosterCard>
    {
        PosterCard();

        [[nodiscard]] winrt::hstring Title() const;
        void Title(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring Meta() const;
        void Meta(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring Poster() const;
        void Poster(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring KindLabel() const;
        void KindLabel(winrt::hstring const& value);
        [[nodiscard]] bool ShowKindBadge() const noexcept;
        void ShowKindBadge(bool value);
        [[nodiscard]] double CardWidth() const noexcept;

        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        winrt::event_token Click(Microsoft::UI::Xaml::RoutedEventHandler const& handler);
        void Click(winrt::event_token const& token) noexcept;

        void OnCardClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCardPointerEntered(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnCardPointerExited(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnPosterOpened(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPosterFailed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::ExceptionRoutedEventArgs const&);

    private:
        [[nodiscard]] Microsoft::UI::Xaml::Controls::Border ArtBorderControl() const;
        void ApplyLayoutMetrics();
        void ShowPlaceholder(bool visible);

        winrt::hstring m_title;
        winrt::hstring m_meta;
        winrt::hstring m_poster;
        winrt::hstring m_kindLabel;
        bool m_showKindBadge{};
        double m_cardWidth{ 132.0 };
        std::uint64_t m_metricsToken{};
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_click;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct PosterCard : PosterCardT<PosterCard, implementation::PosterCard> {};
}
