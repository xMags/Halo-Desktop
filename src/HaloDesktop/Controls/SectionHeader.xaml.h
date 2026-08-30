#pragma once

#include "SectionHeader.g.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct SectionHeader : SectionHeaderT<SectionHeader>
    {
        SectionHeader();

        // Horizontal padding is the page gutter, which on pages that follow the
        // layout step is 24, 30 or 36 rather than a constant. Exposed instead of
        // fixed so a page whose content moves with the step can keep its title
        // on the same line as the content below it; pages that never set it keep
        // the 24 declared in the markup.
        [[nodiscard]] Microsoft::UI::Xaml::Thickness ContentPadding() const noexcept;
        void ContentPadding(Microsoft::UI::Xaml::Thickness const& value);

        [[nodiscard]] winrt::hstring Title() const;
        void Title(winrt::hstring const& value);

        [[nodiscard]] winrt::Windows::Foundation::IInspectable RightContent() const;
        void RightContent(winrt::Windows::Foundation::IInspectable const& value);

        [[nodiscard]] bool ShowBack() const noexcept;
        void ShowBack(bool value);

        winrt::event_token BackClick(Microsoft::UI::Xaml::RoutedEventHandler const& handler);
        void BackClick(winrt::event_token const& token) noexcept;
        void OnBackClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        [[nodiscard]] Microsoft::UI::Xaml::Controls::TextBlock TitleText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Controls::ContentPresenter RightContentHost() const;
        [[nodiscard]] Microsoft::UI::Xaml::Controls::Button BackButton() const;
        [[nodiscard]] Microsoft::UI::Xaml::Controls::Grid HeaderGrid() const;

        // Must match the Padding literal in SectionHeader.xaml.
        Microsoft::UI::Xaml::Thickness m_contentPadding{ 24.0, 12.0, 24.0, 12.0 };
        winrt::hstring m_title;
        winrt::Windows::Foundation::IInspectable m_rightContent{ nullptr };
        bool m_showBack{};
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_backClick;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SectionHeader : SectionHeaderT<SectionHeader, implementation::SectionHeader>
    {
    };
}
