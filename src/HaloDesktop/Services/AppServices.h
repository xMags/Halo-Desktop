#pragma once

#include <memory>

namespace HaloDesktop::Playback
{
    class IPlaybackEngine;
    class IScrubPreviewSource;
    class SubtitleController;
    class UpNextResolver;
}

namespace HaloDesktop::Shell
{
    class LayoutMetricsService;
    class WindowBackdropService;
    class WindowPresentationService;
}

namespace HaloDesktop::Storage
{
    class AppStoragePaths;
}

namespace HaloDesktop::Services::Device
{
    class DeviceDataMove;
}

namespace HaloDesktop::Services::Tmdb
{
    class TmdbKeyStore;
}

namespace HaloDesktop::Services
{
    class AccountPictureService;
    class IAddonService;
    class ICatalogService;
    class IDownloadService;
    class IMetadataService;
    class ISessionService;
    class ISourceService;
    class DevicePreferencesStore;
    class DiscordPresenceService;
    class NavigationService;
    class SearchService;
    class TitleLookupService;
    class ServerFeaturesService;
    class PeopleService;
    class MagnetService;
    class PosterCheckService;
    class PlaybackPreferences;
    class ThemeService;
    class SettingsSyncService;
    class LibraryService;
    class WatchStateService;

    struct AppServices final
    {
        std::shared_ptr<ICatalogService> Catalog;
        std::shared_ptr<SearchService> Search;
        // The title a pasted IMDb id names, for Search's action card.
        std::shared_ptr<TitleLookupService> TitleLookup;
        // What the account's server offers beyond the shared contract.
        std::shared_ptr<ServerFeaturesService> ServerFeatures;
        // People search, person pages and id lookups: on the account's server,
        // or without an account from TMDB with the viewer's own key.
        std::shared_ptr<PeopleService> People;
        // The viewer's own TMDB key, for people search without an account.
        std::shared_ptr<Tmdb::TmdbKeyStore> TmdbKey;
        // Pasted magnets on the account's server, and the last one that came
        // back ready, offered in the Sources sheet.
        std::shared_ptr<MagnetService> Magnets;
        // Whether a poster link leads to an image, asked before Search shows results.
        std::shared_ptr<PosterCheckService> PosterChecks;
        std::shared_ptr<IMetadataService> Metadata;
        std::shared_ptr<ISourceService> Sources;
        std::shared_ptr<IDownloadService> Downloads;
        std::shared_ptr<IAddonService> Addons;
        std::shared_ptr<ISessionService> Session;
        // Moves this PC's data from Halo without an account into an account.
        std::shared_ptr<Device::DeviceDataMove> DeviceMove;
        std::shared_ptr<AccountPictureService> AccountPicture;
        std::shared_ptr<NavigationService> Navigation;
        std::shared_ptr<::HaloDesktop::Storage::AppStoragePaths const> StoragePaths;
        std::shared_ptr<DevicePreferencesStore> DevicePreferences;
        std::shared_ptr<PlaybackPreferences> PlaybackPreferences;
        std::shared_ptr<ThemeService> Theme;
        std::shared_ptr<SettingsSyncService> SettingsSync;
        std::shared_ptr<LibraryService> Library;
        std::shared_ptr<WatchStateService> WatchState;
        std::shared_ptr<DiscordPresenceService> DiscordPresence;
        std::shared_ptr<::HaloDesktop::Playback::IPlaybackEngine> Playback;
        std::shared_ptr<::HaloDesktop::Playback::IScrubPreviewSource> ScrubPreview;
        std::shared_ptr<::HaloDesktop::Playback::SubtitleController> Subtitles;
        std::shared_ptr<::HaloDesktop::Playback::UpNextResolver> UpNext;
        std::shared_ptr<::HaloDesktop::Shell::WindowPresentationService> WindowPresentation;
        std::shared_ptr<::HaloDesktop::Shell::LayoutMetricsService> LayoutMetrics;
        std::shared_ptr<::HaloDesktop::Shell::WindowBackdropService> WindowBackdrop;
    };
}
