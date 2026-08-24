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

    private:
        [[nodiscard]] Microsoft::UI::Xaml::Controls::TextBlock TitleText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Controls::ContentPresenter RightContentHost() const;

        winrt::hstring m_title;
        winrt::Windows::Foundation::IInspectable m_rightContent{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SectionHeader : SectionHeaderT<SectionHeader, implementation::SectionHeader>
    {
    };
}
