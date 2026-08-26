#pragma once

#include "SectionHeader.g.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct SectionHeader : SectionHeaderT<SectionHeader>
    {
        SectionHeader();

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
