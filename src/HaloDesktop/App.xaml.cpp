#include "pch.h"
#include "App.xaml.h"
#include "Api/Addons/HttpAddonFetcher.h"
#include "Api/ApiClient.h"
#include "Api/BackendSelector.h"
#include "Api/HttpExecutor.h"
#include "Config/ServerConfig.h"
#include "Diagnostics/CrashLog.h"
#include "Models/Models.h"
#include "Playback/MpvEngine.h"
#include "Playback/MpvScrubPreviewSource.h"
#include "Playback/NullEngine.h"
#include "Playback/NullScrubPreviewSource.h"
#include "Playback/SubtitleController.h"
#include "Playback/UpNextResolver.h"
#include "Services/AccountPictureService.h"
#include "Services/AddonService.h"
#include "Services/CatalogService.h"
#include "Services/SearchService.h"
#include "Services/ContinueArtworkService.h"
#include "Services/ContinueNextEpisodeService.h"
#include "Services/DownloadArtworkService.h"
#include "Services/DownloadService.h"
#include "Services/DiscordPresence.h"
#include "Services/DevicePreferencesStore.h"
#include "Services/Device/DeviceDataMove.h"
#include "Services/Device/DeviceStore.h"
#include "Services/Downloads/TransferEngine.h"
#include "Services/Auth/LocalAuthSession.h"
#include "Services/Auth/OidcAuthSession.h"
#include "Services/Auth/OidcSignInFlow.h"
#include "Services/Auth/SessionController.h"
#include "Services/Auth/SessionStore.h"
#include "Services/Auth/SignInAddon.h"
#include "Services/Auth/SignInAddonRegistry.h"
#include "Services/NavigationService.h"
#include "Services/LibraryService.h"
#include "Services/MetadataService.h"
#include "Services/PlaybackPreferences.h"
#include "Services/QueryCache.h"
#include "Services/SessionService.h"
#include "Services/SettingsSyncService.h"
#include "Services/SourceService.h"
#include "Services/WatchOutbox.h"
#include "Services/WatchStateService.h"
#include "Services/ThemeService.h"
#include "Services/Downloads/DownloadTypes.h"
#include "Shell/MainWindow.xaml.h"
#include "Shell/LayoutMetricsService.h"
#include "Shell/WindowBackdropService.h"
#include "Shell/WindowPresentationService.h"
#include "Storage/AppStoragePaths.h"
#include "Storage/LegacyPackageDataSource.h"
#include "Storage/PackagedDataMigrator.h"

#include <shobjidl_core.h>

#include <iomanip>
#include <memory>
#include <sstream>
#include <string_view>

namespace winrt::HaloDesktop::implementation
{
    App::App()
    {
        // Unpackaged processes otherwise get a taskbar identity derived from
        // the executable path, which would not match the identity the installer
        // stamps on its shortcuts. Declaring the same explicit id in both
        // places keeps a pinned shortcut and the running window as one taskbar
        // entry. This must happen before any window exists.
        static_cast<void>(SetCurrentProcessExplicitAppUserModelID(L"HaloDesktop.App"));

        m_storagePaths = std::make_shared<::HaloDesktop::Storage::AppStoragePaths>();
        m_crashLog = std::make_shared<::HaloDesktop::Diagnostics::CrashLog>(m_storagePaths->Root() / L"logs");
        auto legacySource = std::make_shared<::HaloDesktop::Storage::InstalledLegacyPackageDataSource>();
        ::HaloDesktop::Storage::PackagedDataMigrator migrator{ m_storagePaths, legacySource };
        static_cast<void>(migrator.Migrate());
        m_storagePaths->EnsureDirectories();
        m_devicePreferences = std::make_shared<::HaloDesktop::Services::DevicePreferencesStore>(
            m_storagePaths->PreferencesFile());
        m_playbackPreferences = std::make_shared<::HaloDesktop::Services::PlaybackPreferences>(
            m_devicePreferences);
        m_discordPresence = std::make_shared<::HaloDesktop::Services::DiscordPresenceService>(
            m_playbackPreferences->DiscordPresenceEnabled());
        m_services.StoragePaths = m_storagePaths;
        m_services.DevicePreferences = m_devicePreferences;
        m_services.PlaybackPreferences = m_playbackPreferences;
        m_services.DiscordPresence = m_discordPresence;

        m_httpExecutor = std::make_shared<::HaloDesktop::Api::HttpExecutor>();
        m_queryCache = std::make_shared<::HaloDesktop::Services::QueryCache>();
        m_services.Navigation = std::make_shared<::HaloDesktop::Services::NavigationService>();
        m_sessionStore = std::make_shared<::HaloDesktop::Services::Auth::SessionStore>(
            m_storagePaths->LocalState());
        m_localAuthSession = std::make_shared<::HaloDesktop::Services::Auth::LocalAuthSession>(
            ::HaloDesktop::Config::ServerBaseUrl,
            m_httpExecutor,
            m_sessionStore);
        m_oidcAuthSession = std::make_shared<::HaloDesktop::Services::Auth::OidcAuthSession>(
            m_httpExecutor,
            m_sessionStore);
        m_oidcSignInFlow = std::make_shared<::HaloDesktop::Services::Auth::OidcSignInFlow>(
            m_httpExecutor);
        // Empty in a public build; see Services/Auth/SignInAddon.h.
        auto const signInAddons = std::make_shared<::HaloDesktop::Services::Auth::SignInAddonRegistry const>(
            ::HaloDesktop::Services::Auth::CreateSignInAddons(::HaloDesktop::Services::Auth::SignInAddonContext{
                .Http = m_httpExecutor,
                .LocalState = m_storagePaths->LocalState(),
            }));
        m_sessionController = std::make_shared<::HaloDesktop::Services::Auth::SessionController>(
            m_sessionStore,
            m_localAuthSession,
            m_oidcAuthSession,
            signInAddons,
            m_queryCache,
            Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread());
        m_apiClient = std::make_shared<::HaloDesktop::Api::ApiClient>(
            ::HaloDesktop::Config::ServerBaseUrl,
            m_httpExecutor,
            m_sessionController);
        // What the services ask for addons, resolution, library, watch history
        // and settings: the account's client, or this PC's backend while Halo
        // is used without an account (switched in the identity handler below).
        m_backends = std::make_shared<::HaloDesktop::Api::BackendSelector>(m_apiClient);
        // Halo without an account: its documents, and the one client that asks
        // addons directly (the device backend and its subtitle files).
        m_deviceStore = std::make_shared<::HaloDesktop::Services::Device::DeviceStore>(
            m_storagePaths->LocalState() / L"device");
        m_addonFetcher = std::make_shared<::HaloDesktop::Api::Addons::HttpAddonFetcher>();
        m_sessionService = std::make_shared<::HaloDesktop::Services::SessionService>(
            m_apiClient,
            m_sessionController,
            m_sessionStore,
            m_oidcSignInFlow,
            signInAddons,
            m_devicePreferences,
            m_services.Navigation,
            m_deviceStore,
            m_addonFetcher);
        std::weak_ptr<::HaloDesktop::Services::SessionService> weakSession = m_sessionService;
        m_sessionController->SetRejectedHandler([weakSession]()
        {
            if (auto const session = weakSession.lock())
            {
                session->HandleSessionRejected();
            }
        });

        auto addonService = std::make_shared<::HaloDesktop::Services::AddonService>(
            m_apiClient,
            m_backends,
            m_queryCache,
            m_sessionService);
        m_services.Addons = addonService;
        auto libraryService = std::make_shared<::HaloDesktop::Services::LibraryService>(m_backends);
        m_services.Library = libraryService;
        auto watchStateService = std::make_shared<::HaloDesktop::Services::WatchStateService>(
            m_backends,
            std::make_shared<::HaloDesktop::Services::WatchOutbox>(m_storagePaths->LocalState()));
        m_services.WatchState = watchStateService;
        // Progress left unsent (offline, or a report the window closed on) goes out
        // again as soon as the network returns.
        watchStateService->StartNetworkWatch(Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread());
        // Owned by the catalog service alone: nothing else needs a continue still,
        // and the cache is only useful next to the shelf that reads it.
        auto continueArtworkService = std::make_shared<::HaloDesktop::Services::ContinueArtworkService>(
            m_backends,
            Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread());
        auto continueNextEpisodeService = std::make_shared<::HaloDesktop::Services::ContinueNextEpisodeService>(
            m_backends,
            Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread());
        // Built ahead of the catalog, which asks its language rows in the
        // account's synced catalog language.
        auto settingsSyncService = std::make_shared<::HaloDesktop::Services::SettingsSyncService>(
            m_backends,
            m_queryCache,
            m_storagePaths,
            Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread());
        m_services.SettingsSync = settingsSyncService;
        auto catalogService = std::make_shared<::HaloDesktop::Services::CatalogService>(
            m_backends,
            addonService,
            m_services.Library,
            m_services.WatchState,
            continueArtworkService,
            continueNextEpisodeService,
            settingsSyncService);
        m_services.Catalog = catalogService;
        auto searchService = std::make_shared<::HaloDesktop::Services::SearchService>(
            m_backends,
            addonService,
            libraryService,
            m_devicePreferences);
        m_services.Search = searchService;
        m_downloadEngine = std::make_shared<::HaloDesktop::Services::Downloads::TransferEngine>(
            m_storagePaths->LocalState(),
            [crashLog = m_crashLog](std::wstring_view line) { crashLog->Record(line); });
        auto downloadArtworkService = std::make_shared<::HaloDesktop::Services::DownloadArtworkService>(
            m_backends);
        m_downloadService = std::make_shared<::HaloDesktop::Services::DownloadService>(
            m_downloadEngine,
            m_sessionService,
            m_devicePreferences,
            downloadArtworkService,
            Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread());
        m_services.Downloads = m_downloadService;
        auto metadataService = std::make_shared<::HaloDesktop::Services::MetadataService>(
            m_backends, m_services.WatchState, m_services.Downloads);
        m_services.Metadata = metadataService;
        auto sourceService = std::make_shared<::HaloDesktop::Services::SourceService>(
            m_apiClient,
            m_backends,
            m_services.Downloads,
            m_services.SettingsSync);
        m_services.Sources = sourceService;
        auto accountPictureService = std::make_shared<::HaloDesktop::Services::AccountPictureService>(m_apiClient);
        m_services.AccountPicture = accountPictureService;
        std::weak_ptr<::HaloDesktop::Services::AccountPictureService> weakAccountPicture = accountPictureService;
        std::weak_ptr<::HaloDesktop::Services::AddonService> weakAddons = addonService;
        std::weak_ptr<::HaloDesktop::Services::LibraryService> weakLibrary = libraryService;
        std::weak_ptr<::HaloDesktop::Services::WatchStateService> weakWatchState = watchStateService;
        std::weak_ptr<::HaloDesktop::Services::CatalogService> weakCatalog = catalogService;
        std::weak_ptr<::HaloDesktop::Services::SearchService> weakSearch = searchService;
        std::weak_ptr<::HaloDesktop::Services::MetadataService> weakMetadata = metadataService;
        std::weak_ptr<::HaloDesktop::Services::SettingsSyncService> weakSettings = settingsSyncService;
        std::weak_ptr<::HaloDesktop::Services::SourceService> weakSources = sourceService;
        std::weak_ptr<::HaloDesktop::Services::DownloadService> weakDownloads = m_downloadService;
        std::weak_ptr<::HaloDesktop::Services::DiscordPresenceService> weakPresence = m_discordPresence;
        m_sessionService->SetIdentityChangedHandler(
            [weakSession,
             backends = m_backends,
             queryCache = m_queryCache,
             weakAddons,
             weakLibrary,
             weakWatchState,
             weakCatalog,
             weakSearch,
             weakMetadata,
             weakSettings,
             weakSources,
             weakDownloads,
             weakPresence,
             weakAccountPicture]()
            {
                // Every switch between an account and this PC's data runs this
                // path, so the backend follows the session before anything below
                // asks it.
                auto const session = weakSession.lock();
                auto const device = session ? session->ActiveDeviceBackend() : nullptr;
                if (device)
                {
                    backends->UseDevice(device);
                }
                else
                {
                    backends->UseAccount();
                }
                // The account (or this PC's profile) the data kept here belongs to.
                std::optional<::HaloDesktop::Services::LocalDataOwner> owner;
                if (session)
                {
                    owner = session->DataOwner();
                }
                if (auto const presence = weakPresence.lock()) presence->Clear();
                if (auto const picture = weakAccountPicture.lock()) picture->OnAccountChanged();
                queryCache->Clear();
                if (auto const addons = weakAddons.lock()) addons->OnAccountChanged();
                if (auto const library = weakLibrary.lock()) library->OnAccountChanged();
                if (auto const watchState = weakWatchState.lock())
                {
                    // The unsent progress belongs to one account on one server (or
                    // to this PC's profile); the key names both, so a change of
                    // either clears it before anything can be sent. Empty while
                    // nobody is signed in.
                    std::wstring accountKey;
                    if (owner)
                    {
                        try
                        {
                            accountKey = ::HaloDesktop::Services::Downloads::MakeAccountKey(owner->Server, owner->User);
                        }
                        catch (...)
                        {
                            accountKey.clear();
                        }
                    }
                    watchState->OnAccountChanged(std::move(accountKey));
                }
                if (auto const catalog = weakCatalog.lock()) catalog->OnAccountChanged();
                if (auto const search = weakSearch.lock()) search->OnAccountChanged();
                if (auto const metadata = weakMetadata.lock()) metadata->OnAccountChanged();
                if (auto const sources = weakSources.lock()) sources->OnAccountChanged();
                if (auto const settings = weakSettings.lock())
                {
                    settings->OnAccountChanged(
                        owner ? winrt::hstring{ owner->Server } : winrt::hstring{},
                        owner ? winrt::hstring{ owner->User } : winrt::hstring{});
                }
                if (auto const downloads = weakDownloads.lock()) downloads->RebindAccount();
            });
        m_services.Session = m_sessionService;
        m_services.DeviceMove = std::make_shared<::HaloDesktop::Services::Device::DeviceDataMove>(
            m_sessionService,
            m_apiClient,
            m_deviceStore,
            m_addonFetcher,
            m_services.Downloads);
        m_services.Theme = std::make_shared<::HaloDesktop::Services::ThemeService>(m_devicePreferences);
#if (defined(_M_X64) || defined(_M_ARM64)) && !defined(HALO_USE_NULL_PLAYBACK)
        m_services.Playback = std::make_shared<::HaloDesktop::Playback::MpvEngine>(m_playbackPreferences);
        m_services.ScrubPreview = std::make_shared<::HaloDesktop::Playback::MpvScrubPreviewSource>();
#else
        m_services.Playback = std::make_shared<::HaloDesktop::Playback::NullEngine>();
        m_services.ScrubPreview = std::make_shared<::HaloDesktop::Playback::NullScrubPreviewSource>();
#endif
        m_services.Subtitles=std::make_shared<::HaloDesktop::Playback::SubtitleController>(m_apiClient,m_backends,m_addonFetcher,m_services.Playback,m_services.SettingsSync,m_services.Downloads,m_devicePreferences,m_storagePaths);
        m_services.UpNext=std::make_shared<::HaloDesktop::Playback::UpNextResolver>(m_backends,m_services.SettingsSync,m_services.Downloads);
        m_services.WindowPresentation = std::make_shared<::HaloDesktop::Shell::WindowPresentationService>();
        m_services.LayoutMetrics = std::make_shared<::HaloDesktop::Shell::LayoutMetricsService>();
        m_services.WindowBackdrop = std::make_shared<::HaloDesktop::Shell::WindowBackdropService>(m_devicePreferences);

        // Only the error code is written: an exception's message can carry a
        // server sentence, a path or a URL, none of which belong in the log.
        UnhandledException([crashLog = m_crashLog](
            [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e)
        {
            std::wostringstream line;
            line << L"unhandled exception 0x" << std::hex << std::setw(8) << std::setfill(L'0')
                 << static_cast<std::uint32_t>(e.Exception().value);
            crashLog->Record(line.str());
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
            if (IsDebuggerPresent())
            {
                [[maybe_unused]] auto const errorMessage = e.Message();
                __debugbreak();
            }
#endif
        });
    }

    ::HaloDesktop::Services::AppServices& App::Services()
    {
        auto const overrides = Microsoft::UI::Xaml::Application::Current()
            .as<Microsoft::UI::Xaml::IApplicationOverrides>();
        return winrt::get_self<App>(overrides)->m_services;
    }

    Microsoft::UI::Xaml::Window App::Window()
    {
        auto const overrides = Microsoft::UI::Xaml::Application::Current()
            .as<Microsoft::UI::Xaml::IApplicationOverrides>();
        return winrt::get_self<App>(overrides)->m_window;
    }

    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& e)
    {
        static_cast<void>(e);
        static_cast<void>(LaunchAsync());
    }

    winrt::Windows::Foundation::IAsyncAction App::LaunchAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        try
        {
            co_await m_sessionService->RestoreAsync();
        }
        catch (...)
        {
        }
        co_await uiContext;

        m_window = winrt::make<MainWindow>();
        m_window.Activate();
        if (m_sessionService->Mode() == ::HaloDesktop::Services::SessionMode::Device)
        {
            // A first run without an account may have had no network for the
            // built-in addons, and a newer version may add one. Never blocks
            // the first page and never reports a failure.
            auto const generation = m_sessionService->Generation();
            bool changed{};
            try
            {
                changed = co_await m_sessionService->TopUpDeviceAddonsAsync();
            }
            catch (...)
            {
                changed = false;
            }
            co_await uiContext;
            if (changed && generation == m_sessionService->Generation())
            {
                m_sessionService->ReloadAccountData();
            }
            co_return;
        }
        if (m_sessionService->IsSignedIn())
        {
            bool loaded{};
            try
            {
                loaded = co_await m_sessionService->RefreshIdentityAsync();
            }
            catch (...)
            {
                loaded = false;
            }
            co_await uiContext;
            // Finishes moving this PC's data into the account when an earlier
            // sign-in could not; tried once per launch.
            if (loaded && m_sessionService->HasDeviceDataToMove())
            {
                // Seeds too: the addon service leaves seeding to the move
                // while one is pending.
                static_cast<void>(co_await m_services.DeviceMove->RunAsync(true));
            }
        }
    }


}
