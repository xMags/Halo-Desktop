#pragma once

#include "NavRailItem.g.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

namespace winrt::HaloDesktop::implementation
{
    // See NavRailItem.idl. The look is HaloNavRailItemStyle's template; this
    // adds its properties and plays the glyph's motion as the item lights. The
    // shell navigates on Click. UI thread only.
    struct NavRailItem : NavRailItemT<NavRailItem>
    {
        NavRailItem();

        [[nodiscard]] winrt::hstring Label() const;
        void Label(winrt::hstring const& value);
        [[nodiscard]] static Microsoft::UI::Xaml::DependencyProperty LabelProperty();

        [[nodiscard]] Microsoft::UI::Xaml::Media::Geometry Glyph() const;
        void Glyph(Microsoft::UI::Xaml::Media::Geometry const& value);
        [[nodiscard]] static Microsoft::UI::Xaml::DependencyProperty GlyphProperty();

        [[nodiscard]] Microsoft::UI::Xaml::Media::Geometry SelectedGlyph() const;
        void SelectedGlyph(Microsoft::UI::Xaml::Media::Geometry const& value);
        [[nodiscard]] static Microsoft::UI::Xaml::DependencyProperty SelectedGlyphProperty();

        [[nodiscard]] winrt::HaloDesktop::NavRailMotion Motion() const noexcept;
        void Motion(winrt::HaloDesktop::NavRailMotion value) noexcept;

        void OnApplyTemplate();

    private:
        void PlayMotion();

        winrt::HaloDesktop::NavRailMotion m_motion{ winrt::HaloDesktop::NavRailMotion::None };
        // The template's figure transform the motion plays on, and the motion
        // running on it (a newer one stops it). Null until the template applies.
        Microsoft::UI::Xaml::Media::CompositeTransform m_figure{ nullptr };
        Microsoft::UI::Xaml::Media::Animation::Storyboard m_running{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct NavRailItem : NavRailItemT<NavRailItem, implementation::NavRailItem> {};
}
