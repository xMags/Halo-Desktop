#pragma once

#include "LibraryPage.g.h"

namespace winrt::HaloDesktop::implementation
{
    struct LibraryPage : LibraryPageT<LibraryPage>
    {
        LibraryPage();
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct LibraryPage : LibraryPageT<LibraryPage, implementation::LibraryPage>
    {
    };
}
