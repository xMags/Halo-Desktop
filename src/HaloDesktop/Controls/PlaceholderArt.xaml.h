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

    private:
        [[nodiscard]] Microsoft::UI::Xaml::Controls::TextBlock CaptionText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Controls::Border ArtSurface() const;

        winrt::hstring m_caption;
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
