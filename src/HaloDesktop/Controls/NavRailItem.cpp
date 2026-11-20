#include "pch.h"
#include "Controls/NavRailItem.h"
#if __has_include("NavRailItem.g.cpp")
#include "NavRailItem.g.cpp"
#endif

#include <winrt/Windows.UI.Xaml.Interop.h>

namespace winrt::HaloDesktop::implementation
{
    // Registered on first use, which XAML's type information makes before it
    // sets any of them, rather than during static initialisation, which runs
    // before the XAML runtime is up in a desktop app. The registrations are
    // immutable identities XAML itself holds for the process's lifetime.
    Microsoft::UI::Xaml::DependencyProperty NavRailItem::LabelProperty()
    {
        static Microsoft::UI::Xaml::DependencyProperty const property = Microsoft::UI::Xaml::DependencyProperty::Register(
            L"Label",
            winrt::xaml_typename<winrt::hstring>(),
            winrt::xaml_typename<winrt::HaloDesktop::NavRailItem>(),
            Microsoft::UI::Xaml::PropertyMetadata{ winrt::box_value(winrt::hstring{}) });
        return property;
    }

    Microsoft::UI::Xaml::DependencyProperty NavRailItem::GlyphProperty()
    {
        static Microsoft::UI::Xaml::DependencyProperty const property = Microsoft::UI::Xaml::DependencyProperty::Register(
            L"Glyph",
            winrt::xaml_typename<Microsoft::UI::Xaml::Media::Geometry>(),
            winrt::xaml_typename<winrt::HaloDesktop::NavRailItem>(),
            Microsoft::UI::Xaml::PropertyMetadata{ nullptr });
        return property;
    }

    Microsoft::UI::Xaml::DependencyProperty NavRailItem::SelectedGlyphProperty()
    {
        static Microsoft::UI::Xaml::DependencyProperty const property = Microsoft::UI::Xaml::DependencyProperty::Register(
            L"SelectedGlyph",
            winrt::xaml_typename<Microsoft::UI::Xaml::Media::Geometry>(),
            winrt::xaml_typename<winrt::HaloDesktop::NavRailItem>(),
            Microsoft::UI::Xaml::PropertyMetadata{ nullptr });
        return property;
    }

    winrt::hstring NavRailItem::Label() const
    {
        return winrt::unbox_value_or<winrt::hstring>(GetValue(LabelProperty()), winrt::hstring{});
    }

    void NavRailItem::Label(winrt::hstring const& value)
    {
        SetValue(LabelProperty(), winrt::box_value(value));
    }

    Microsoft::UI::Xaml::Media::Geometry NavRailItem::Glyph() const
    {
        return GetValue(GlyphProperty()).try_as<Microsoft::UI::Xaml::Media::Geometry>();
    }

    void NavRailItem::Glyph(Microsoft::UI::Xaml::Media::Geometry const& value)
    {
        SetValue(GlyphProperty(), value);
    }

    Microsoft::UI::Xaml::Media::Geometry NavRailItem::SelectedGlyph() const
    {
        return GetValue(SelectedGlyphProperty()).try_as<Microsoft::UI::Xaml::Media::Geometry>();
    }

    void NavRailItem::SelectedGlyph(Microsoft::UI::Xaml::Media::Geometry const& value)
    {
        SetValue(SelectedGlyphProperty(), value);
    }
}
