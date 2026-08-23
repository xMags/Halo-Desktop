#pragma once

#include "PlaceholderArt.g.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace winrt::HaloDesktop::implementation
{
    struct PlaceholderArt : PlaceholderArtT<PlaceholderArt>
    {
        PlaceholderArt();

        [[nodiscard]] winrt::hstring Caption() const;
        void Caption(winrt::hstring const& value);

    private:
        [[nodiscard]] Microsoft::UI::Xaml::Controls::TextBlock CaptionText() const;

        winrt::hstring m_caption;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct PlaceholderArt : PlaceholderArtT<PlaceholderArt, implementation::PlaceholderArt>
    {
    };
}
