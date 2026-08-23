#include "pch.h"
#include "Shell/TitleBar.xaml.h"
#if __has_include("TitleBar.g.cpp")
#include "TitleBar.g.cpp"
#endif

namespace winrt::HaloDesktop::implementation
{
    TitleBar::TitleBar() = default;

    Microsoft::UI::Xaml::Controls::TextBlock TitleBar::CrumbText() const
    {
        return FindName(L"CrumbText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>();
    }

    winrt::hstring TitleBar::Crumb() const
    {
        return m_crumb;
    }

    void TitleBar::Crumb(winrt::hstring const& value)
    {
        m_crumb = value;
        if (auto const text = CrumbText())
        {
            text.Text(value);
        }
    }
}
