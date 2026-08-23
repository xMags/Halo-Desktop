#pragma once

#include "SettingsPage.g.h"

namespace winrt::HaloDesktop::implementation
{
    struct SettingsPage : SettingsPageT<SettingsPage>
    {
        SettingsPage();
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SettingsPage : SettingsPageT<SettingsPage, implementation::SettingsPage>
    {
    };
}
