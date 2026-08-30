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

    Microsoft::UI::Xaml::Controls::Grid SectionHeader::HeaderGrid() const
    {
        return FindName(L"HeaderGrid").try_as<Microsoft::UI::Xaml::Controls::Grid>();
    }

    Microsoft::UI::Xaml::Thickness SectionHeader::ContentPadding() const noexcept
    {
        return m_contentPadding;
    }

    void SectionHeader::ContentPadding(Microsoft::UI::Xaml::Thickness const& value)
    {
        m_contentPadding = value;
        if (auto const grid = HeaderGrid())
        {
            grid.Padding(value);
        }
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

    Microsoft::UI::Xaml::Controls::Button SectionHeader::BackButton() const
    {
        return FindName(L"BackButton").try_as<Microsoft::UI::Xaml::Controls::Button>();
    }

    bool SectionHeader::ShowBack() const noexcept
    {
        return m_showBack;
    }

    void SectionHeader::ShowBack(bool value)
    {
        m_showBack = value;
        if (auto const button = BackButton())
        {
            button.Visibility(value ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed);
        }
    }

    winrt::event_token SectionHeader::BackClick(Microsoft::UI::Xaml::RoutedEventHandler const& handler)
    {
        return m_backClick.add(handler);
    }

    void SectionHeader::BackClick(winrt::event_token const& token) noexcept
    {
        m_backClick.remove(token);
    }

    void SectionHeader::OnBackClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_backClick(*this, args);
    }
}
