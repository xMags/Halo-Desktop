#pragma once

#include <memory>

namespace HaloDesktop::Playback
{
    class IPlaybackEngine;
    class SubtitleController;
    class UpNextResolver;
}

namespace HaloDesktop::Shell
{
    class WindowPresentationService;
}

namespace HaloDesktop::Services
{
    class IAddonService;
    class ICatalogService;
    class IDownloadService;
    class IMetadataService;
    class ISessionService;
    class ISourceService;
    class NavigationService;
    class ThemeService;
    class SettingsSyncService;
    class LibraryService;
    class WatchStateService;

    struct AppServices final
    {
        std::shared_ptr<ICatalogService> Catalog;
        std::shared_ptr<IMetadataService> Metadata;
        std::shared_ptr<ISourceService> Sources;
        std::shared_ptr<IDownloadService> Downloads;
        std::shared_ptr<IAddonService> Addons;
        std::shared_ptr<ISessionService> Session;
        std::shared_ptr<NavigationService> Navigation;
        std::shared_ptr<ThemeService> Theme;
        std::shared_ptr<SettingsSyncService> SettingsSync;
        std::shared_ptr<LibraryService> Library;
        std::shared_ptr<WatchStateService> WatchState;
        std::shared_ptr<::HaloDesktop::Playback::IPlaybackEngine> Playback;
        std::shared_ptr<::HaloDesktop::Playback::SubtitleController> Subtitles;
        std::shared_ptr<::HaloDesktop::Playback::UpNextResolver> UpNext;
        std::shared_ptr<::HaloDesktop::Shell::WindowPresentationService> WindowPresentation;
    };
}
