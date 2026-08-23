#include "pch.h"
#include "Controls/TagChip.xaml.h"
#if __has_include("TagChip.g.cpp")
#include "TagChip.g.cpp"
#endif

namespace winrt::HaloDesktop::implementation
{
    TagChip::TagChip() = default;
    winrt::hstring TagChip::Text() const { return m_text; }
    void TagChip::Text(winrt::hstring const& value)
    {
        m_text = value;
        if (auto text = FindName(L"ChipText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>())
        {
            text.Text(value);
        }
    }
}
