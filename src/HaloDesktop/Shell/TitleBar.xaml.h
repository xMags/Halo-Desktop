#pragma once

#include "TitleBar.g.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace winrt::HaloDesktop::implementation
{
    struct TitleBar : TitleBarT<TitleBar>
    {
        TitleBar();

        [[nodiscard]] winrt::hstring Crumb() const;
        void Crumb(winrt::hstring const& value);

    private:
        [[nodiscard]] Microsoft::UI::Xaml::Controls::TextBlock CrumbText() const;

        winrt::hstring m_crumb{ L"· Home" };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct TitleBar : TitleBarT<TitleBar, implementation::TitleBar>
    {
    };
}
