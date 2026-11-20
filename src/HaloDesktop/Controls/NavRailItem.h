#pragma once

#include "NavRailItem.g.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace winrt::HaloDesktop::implementation
{
    // See NavRailItem.idl. Only properties: the look is HaloNavRailItemStyle's
    // template, and the shell navigates on Click.
    struct NavRailItem : NavRailItemT<NavRailItem>
    {
        NavRailItem() = default;

        [[nodiscard]] winrt::hstring Label() const;
        void Label(winrt::hstring const& value);
        [[nodiscard]] static Microsoft::UI::Xaml::DependencyProperty LabelProperty();

        [[nodiscard]] Microsoft::UI::Xaml::Media::Geometry Glyph() const;
        void Glyph(Microsoft::UI::Xaml::Media::Geometry const& value);
        [[nodiscard]] static Microsoft::UI::Xaml::DependencyProperty GlyphProperty();

        [[nodiscard]] Microsoft::UI::Xaml::Media::Geometry SelectedGlyph() const;
        void SelectedGlyph(Microsoft::UI::Xaml::Media::Geometry const& value);
        [[nodiscard]] static Microsoft::UI::Xaml::DependencyProperty SelectedGlyphProperty();
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct NavRailItem : NavRailItemT<NavRailItem, implementation::NavRailItem> {};
}
