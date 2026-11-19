#include "pch.h"
#include "Controls/PlaceholderArt.xaml.h"
#if __has_include("PlaceholderArt.g.cpp")
#include "PlaceholderArt.g.cpp"
#endif

namespace winrt::HaloDesktop::implementation
{
    PlaceholderArt::PlaceholderArt() = default;

    Microsoft::UI::Xaml::Controls::TextBlock PlaceholderArt::CaptionText() const
    {
        return FindName(L"CaptionText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>();
    }

    Microsoft::UI::Xaml::Controls::Border PlaceholderArt::ArtSurface() const
    {
        return FindName(L"ArtSurface").try_as<Microsoft::UI::Xaml::Controls::Border>();
    }

    Microsoft::UI::Xaml::CornerRadius PlaceholderArt::ArtCornerRadius() const noexcept
    {
        return m_artCornerRadius;
    }

    void PlaceholderArt::ArtCornerRadius(Microsoft::UI::Xaml::CornerRadius const& value)
    {
        m_artCornerRadius = value;
        if (auto const surface = ArtSurface())
        {
            surface.CornerRadius(value);
        }
    }

    winrt::HaloDesktop::PlaceholderKind PlaceholderArt::Kind() const noexcept
    {
        return m_kind;
    }

    void PlaceholderArt::Kind(winrt::HaloDesktop::PlaceholderKind value)
    {
        if (m_kind == value) return;
        m_kind = value;
        ApplyKind();
    }

    // A poster kind swaps the stripes for the soft surface and shows only its own glyph.
    void PlaceholderArt::ApplyKind()
    {
        using Microsoft::UI::Xaml::Visibility;
        auto const poster = m_kind != winrt::HaloDesktop::PlaceholderKind::Plain;
        auto const show = [](Microsoft::UI::Xaml::UIElement const& element, bool visible)
        {
            if (element) element.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
        };
        show(FindName(L"Stripes").try_as<Microsoft::UI::Xaml::UIElement>(), !poster);
        show(FindName(L"PosterSurface").try_as<Microsoft::UI::Xaml::UIElement>(), poster);
        show(FindName(L"MovieGlyph").try_as<Microsoft::UI::Xaml::UIElement>(), m_kind == winrt::HaloDesktop::PlaceholderKind::Movie);
        show(FindName(L"SeriesGlyph").try_as<Microsoft::UI::Xaml::UIElement>(), m_kind == winrt::HaloDesktop::PlaceholderKind::Series);
    }

    winrt::hstring PlaceholderArt::Caption() const
    {
        return m_caption;
    }

    void PlaceholderArt::Caption(winrt::hstring const& value)
    {
        m_caption = value;
        if (auto const text = CaptionText())
        {
            text.Text(value);
            text.Visibility(value.empty()
                ? Microsoft::UI::Xaml::Visibility::Collapsed
                : Microsoft::UI::Xaml::Visibility::Visible);
        }
    }
}
