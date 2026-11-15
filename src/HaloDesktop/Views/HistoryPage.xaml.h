#pragma once

#include "HistoryPage.g.h"

namespace winrt::HaloDesktop::implementation
{
    struct HistoryPage : HistoryPageT<HistoryPage>
    {
        HistoryPage() = default;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct HistoryPage : HistoryPageT<HistoryPage, implementation::HistoryPage>
    {
    };
}
