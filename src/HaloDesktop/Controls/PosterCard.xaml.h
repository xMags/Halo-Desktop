#pragma once

#include "PosterCard.g.h"

#include "Controls/CardHoverShadow.h"

#include <cstdint>
#include <string>
#include <vector>
#include <winrt/Microsoft.UI.Composition.h>
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
        [[nodiscard]] winrt::hstring MetaId() const;
        void MetaId(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring KindLabel() const;
        void KindLabel(winrt::hstring const& value);
        [[nodiscard]] bool ShowKindBadge() const noexcept;
        void ShowKindBadge(bool value);
        [[nodiscard]] bool FillsColumn() const noexcept;
        void FillsColumn(bool value);
        [[nodiscard]] double CardWidth() const noexcept;

        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        winrt::event_token Click(Microsoft::UI::Xaml::RoutedEventHandler const& handler);
        void Click(winrt::event_token const& token) noexcept;
        winrt::event_token PosterFailed(Microsoft::UI::Xaml::RoutedEventHandler const& handler);
        void PosterFailed(winrt::event_token const& token) noexcept;

        void OnCardClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCardPointerEntered(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnCardPointerExited(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void OnPosterOpened(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPosterFailed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::ExceptionRoutedEventArgs const&);

    private:
        void ApplyLayoutMetrics();
        void ApplyColumnWidth(double width);
        void ShowPlaceholder(bool visible);
        void SetHovered(bool hovered);
        void RaisePosterFailed();
        // Starts over from the card's own poster.
        void LoadArtwork();
        // The next source not tried yet (the poster, then metahub's), or the
        // placeholder and PosterFailed once none is left.
        void TryNextSource();

        winrt::hstring m_title;
        winrt::hstring m_meta;
        winrt::hstring m_poster;
        winrt::hstring m_metaId;
        std::vector<std::wstring> m_attempted;
        bool m_exhausted{};
        winrt::hstring m_kindLabel;
        bool m_showKindBadge{};
        bool m_fillsColumn{};
        double m_cardWidth{ 132.0 };
        ::HaloDesktop::Controls::CardHoverShadow m_hoverShadow;
        std::uint64_t m_metricsToken{};
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_click;
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_posterFailed;
        Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker m_sizeChanged;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct PosterCard : PosterCardT<PosterCard, implementation::PosterCard> {};
}
