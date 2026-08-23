#pragma once

#include "DetailPage.g.h"

namespace winrt::HaloDesktop::implementation
{
    struct DetailPage : DetailPageT<DetailPage>
    {
        DetailPage();
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct DetailPage : DetailPageT<DetailPage, implementation::DetailPage>
    {
    };
}
