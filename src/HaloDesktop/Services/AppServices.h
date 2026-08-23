#pragma once

#include <memory>

namespace HaloDesktop::Services
{
    class NavigationService;

    struct AppServices final
    {
        std::shared_ptr<NavigationService> Navigation;
    };
}
