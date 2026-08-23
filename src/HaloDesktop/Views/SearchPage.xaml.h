#pragma once

#include "SearchPage.g.h"

namespace winrt::HaloDesktop::implementation
{
    struct SearchPage : SearchPageT<SearchPage>
    {
        SearchPage();
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct SearchPage : SearchPageT<SearchPage, implementation::SearchPage>
    {
    };
}
