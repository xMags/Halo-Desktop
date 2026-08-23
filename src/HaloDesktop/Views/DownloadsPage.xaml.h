#pragma once

#include "DownloadsPage.g.h"

namespace winrt::HaloDesktop::implementation
{
    struct DownloadsPage : DownloadsPageT<DownloadsPage>
    {
        DownloadsPage();
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct DownloadsPage : DownloadsPageT<DownloadsPage, implementation::DownloadsPage>
    {
    };
}
