#pragma once

#include "PlaceholderArt.g.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace winrt::HaloDesktop::implementation
{
    struct PlaceholderArt : PlaceholderArtT<PlaceholderArt>
    {
        PlaceholderArt();

        [[nodiscard]] winrt::hstring Caption() const;
        void Caption(winrt::hstring const& value);

        [[nodiscard]] Microsoft::UI::Xaml::CornerRadius ArtCornerRadius() const noexcept;
        void ArtCornerRadius(Microsoft::UI::Xaml::CornerRadius const& value);

        [[nodiscard]] winrt::HaloDesktop::PlaceholderKind Kind() const noexcept;
        void Kind(winrt::HaloDesktop::PlaceholderKind value);

    private:
        void ApplyKind();

        [[nodiscard]] Microsoft::UI::Xaml::Controls::TextBlock CaptionText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Controls::Border ArtSurface() const;

        winrt::hstring m_caption;
        winrt::HaloDesktop::PlaceholderKind m_kind{ winrt::HaloDesktop::PlaceholderKind::Plain };
        // Must match the CornerRadius literal in PlaceholderArt.xaml, which is
        // HaloControlCornerRadius.
        Microsoft::UI::Xaml::CornerRadius m_artCornerRadius{ 4.0, 4.0, 4.0, 4.0 };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct PlaceholderArt : PlaceholderArtT<PlaceholderArt, implementation::PlaceholderArt>
    {
    };
}
