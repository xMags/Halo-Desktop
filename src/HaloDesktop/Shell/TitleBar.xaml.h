#pragma once

#include "TitleBar.g.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace winrt::HaloDesktop::implementation
{
    struct TitleBar : TitleBarT<TitleBar>
    {
        TitleBar();
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct TitleBar : TitleBarT<TitleBar, implementation::TitleBar>
    {
    };
}
