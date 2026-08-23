#pragma once

#include <memory>

namespace HaloDesktop::Services
{
    class IAddonService;
    class ICatalogService;
    class IDownloadService;
    class IMetadataService;
    class ISessionService;
    class ISourceService;
    class NavigationService;

    struct AppServices final
    {
        std::shared_ptr<ICatalogService> Catalog;
        std::shared_ptr<IMetadataService> Metadata;
        std::shared_ptr<ISourceService> Sources;
        std::shared_ptr<IDownloadService> Downloads;
        std::shared_ptr<IAddonService> Addons;
        std::shared_ptr<ISessionService> Session;
        std::shared_ptr<NavigationService> Navigation;
    };
}
