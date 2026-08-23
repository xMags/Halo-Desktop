#include "pch.h"
#include "Controls/SectionHeader.xaml.h"
#if __has_include("SectionHeader.g.cpp")
#include "SectionHeader.g.cpp"
#endif

namespace winrt::HaloDesktop::implementation
{
    SectionHeader::SectionHeader() = default;

    Microsoft::UI::Xaml::Controls::TextBlock SectionHeader::TitleText() const
    {
        return FindName(L"TitleText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>();
    }

    Microsoft::UI::Xaml::Controls::TextBlock SectionHeader::CrumbText() const
    {
        return FindName(L"CrumbText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>();
    }

    Microsoft::UI::Xaml::Controls::ContentPresenter SectionHeader::RightContentHost() const
    {
        return FindName(L"RightContentHost").try_as<Microsoft::UI::Xaml::Controls::ContentPresenter>();
    }

    winrt::hstring SectionHeader::Title() const
    {
        return m_title;
    }

    void SectionHeader::Title(winrt::hstring const& value)
    {
        m_title = value;
        if (auto const text = TitleText())
        {
            text.Text(value);
        }
    }

    winrt::hstring SectionHeader::Crumb() const
    {
        return m_crumb;
    }

    void SectionHeader::Crumb(winrt::hstring const& value)
    {
        m_crumb = value;
        if (auto const text = CrumbText())
        {
            text.Text(value);
        }
    }

    winrt::Windows::Foundation::IInspectable SectionHeader::RightContent() const
    {
        return m_rightContent;
    }

    void SectionHeader::RightContent(winrt::Windows::Foundation::IInspectable const& value)
    {
        m_rightContent = value;
        if (auto const presenter = RightContentHost())
        {
            presenter.Content(value);
        }
    }
}
