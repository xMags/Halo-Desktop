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
