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
