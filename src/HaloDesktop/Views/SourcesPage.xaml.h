#pragma once

#include "SourcesPage.g.h"

namespace winrt::HaloDesktop::implementation
{
    struct SourcesPage : SourcesPageT<SourcesPage>
    {
        SourcesPage();
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SourcesPage : SourcesPageT<SourcesPage, implementation::SourcesPage>
    {
    };
}
