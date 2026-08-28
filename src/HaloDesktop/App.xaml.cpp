#include "pch.h"
#include "App.xaml.h"
#include "Api/ApiClient.h"
#include "Api/HttpExecutor.h"
#include "Config/ServerConfig.h"
#include "Models/Models.h"
#include "Playback/MpvEngine.h"
#include "Playback/NullEngine.h"
#include "Playback/SubtitleController.h"
#include "Playback/UpNextResolver.h"
#include "Services/AddonService.h"
#include "Services/CatalogService.h"
#include "Services/DownloadService.h"
#include "Services/DevicePreferencesStore.h"
#include "Services/Downloads/TransferEngine.h"
#include "Services/Auth/LocalAuthSession.h"
#include "Services/Auth/OidcAuthSession.h"
#include "Services/Auth/OidcSignInFlow.h"
#include "Services/Auth/SessionController.h"
#include "Services/Auth/SessionStore.h"
#include "Services/NavigationService.h"
#include "Services/LibraryService.h"
#include "Services/MetadataService.h"
#include "Services/PlaybackPreferences.h"
#include "Services/QueryCache.h"
#include "Services/SessionService.h"
#include "Services/SettingsSyncService.h"
#include "Services/SourceService.h"
#include "Services/WatchStateService.h"
#include "Services/ThemeService.h"
#include "Shell/MainWindow.xaml.h"
#include "Shell/LayoutMetricsService.h"
#include "Shell/WindowPresentationService.h"
#include "Storage/AppStoragePaths.h"
#include "Storage/LegacyPackageDataSource.h"
#include "Storage/PackagedDataMigrator.h"

#include <shobjidl_core.h>

#include <memory>

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
        auto legacySource = std::make_shared<::HaloDesktop::Storage::InstalledLegacyPackageDataSource>();
        ::HaloDesktop::Storage::PackagedDataMigrator migrator{ m_storagePaths, legacySource };
        static_cast<void>(migrator.Migrate());
        m_storagePaths->EnsureDirectories();
        m_devicePreferences = std::make_shared<::HaloDesktop::Services::DevicePreferencesStore>(
            m_storagePaths->PreferencesFile());
        m_playbackPreferences = std::make_shared<::HaloDesktop::Services::PlaybackPreferences>(
            m_devicePreferences);
        m_services.StoragePaths = m_storagePaths;
        m_services.DevicePreferences = m_devicePreferences;
        m_services.PlaybackPreferences = m_playbackPreferences;

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
        m_sessionController = std::make_shared<::HaloDesktop::Services::Auth::SessionController>(
            m_sessionStore,
            m_localAuthSession,
            m_oidcAuthSession,
            m_queryCache,
            Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread());
        m_apiClient = std::make_shared<::HaloDesktop::Api::ApiClient>(
            ::HaloDesktop::Config::ServerBaseUrl,
            m_httpExecutor,
            m_sessionController);
        m_sessionService = std::make_shared<::HaloDesktop::Services::SessionService>(
            m_apiClient,
            m_sessionController,
            m_sessionStore,
            m_oidcSignInFlow,
            m_services.Navigation);
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
            m_queryCache,
            m_sessionService);
        m_services.Addons = addonService;
        auto libraryService = std::make_shared<::HaloDesktop::Services::LibraryService>(m_apiClient);
        m_services.Library = libraryService;
        auto watchStateService = std::make_shared<::HaloDesktop::Services::WatchStateService>(m_apiClient);
        m_services.WatchState = watchStateService;
        auto catalogService = std::make_shared<::HaloDesktop::Services::CatalogService>(
            m_apiClient,
            addonService,
            m_services.Library,
            m_services.WatchState,
            m_devicePreferences);
        m_services.Catalog = catalogService;
        m_downloadEngine = std::make_shared<::HaloDesktop::Services::Downloads::TransferEngine>(
            m_storagePaths->LocalState());
        m_downloadService = std::make_shared<::HaloDesktop::Services::DownloadService>(
            m_downloadEngine,
            m_sessionService,
            m_devicePreferences,
            Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread());
        m_services.Downloads = m_downloadService;
        auto metadataService = std::make_shared<::HaloDesktop::Services::MetadataService>(
            m_apiClient, m_services.WatchState, m_services.Downloads);
        m_services.Metadata = metadataService;
        auto settingsSyncService = std::make_shared<::HaloDesktop::Services::SettingsSyncService>(
            m_apiClient,
            m_queryCache,
            m_storagePaths,
            Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread());
        m_services.SettingsSync = settingsSyncService;
        auto sourceService = std::make_shared<::HaloDesktop::Services::SourceService>(
            m_apiClient,
            m_services.Downloads,
            m_services.SettingsSync);
        m_services.Sources = sourceService;
        std::weak_ptr<::HaloDesktop::Services::AddonService> weakAddons = addonService;
        std::weak_ptr<::HaloDesktop::Services::LibraryService> weakLibrary = libraryService;
        std::weak_ptr<::HaloDesktop::Services::WatchStateService> weakWatchState = watchStateService;
        std::weak_ptr<::HaloDesktop::Services::CatalogService> weakCatalog = catalogService;
        std::weak_ptr<::HaloDesktop::Services::MetadataService> weakMetadata = metadataService;
        std::weak_ptr<::HaloDesktop::Services::SettingsSyncService> weakSettings = settingsSyncService;
        std::weak_ptr<::HaloDesktop::Services::SourceService> weakSources = sourceService;
        std::weak_ptr<::HaloDesktop::Services::DownloadService> weakDownloads = m_downloadService;
        m_sessionService->SetIdentityChangedHandler(
            [weakSession,
             queryCache = m_queryCache,
             weakAddons,
             weakLibrary,
             weakWatchState,
             weakCatalog,
             weakMetadata,
             weakSettings,
             weakSources,
             weakDownloads]()
            {
                queryCache->Clear();
                if (auto const addons = weakAddons.lock()) addons->OnAccountChanged();
                if (auto const library = weakLibrary.lock()) library->OnAccountChanged();
                if (auto const watchState = weakWatchState.lock()) watchState->OnAccountChanged();
                if (auto const catalog = weakCatalog.lock()) catalog->OnAccountChanged();
                if (auto const metadata = weakMetadata.lock()) metadata->OnAccountChanged();
                if (auto const sources = weakSources.lock()) sources->OnAccountChanged();
                if (auto const settings = weakSettings.lock())
                {
                    if (auto const session = weakSession.lock())
                    {
                        settings->OnAccountChanged(session->ServerUrl(), session->UserId());
                    }
                }
                if (auto const downloads = weakDownloads.lock()) downloads->RebindAccount();
            });
        m_services.Session = m_sessionService;
        m_services.Theme = std::make_shared<::HaloDesktop::Services::ThemeService>(m_devicePreferences);
#if defined(_M_X64) && !defined(HALO_USE_NULL_PLAYBACK)
        m_services.Playback = std::make_shared<::HaloDesktop::Playback::MpvEngine>(m_playbackPreferences);
#else
        m_services.Playback = std::make_shared<::HaloDesktop::Playback::NullEngine>();
#endif
        m_services.Subtitles=std::make_shared<::HaloDesktop::Playback::SubtitleController>(m_apiClient,m_services.Playback,m_services.SettingsSync,m_services.Downloads,m_devicePreferences,m_storagePaths);
        m_services.UpNext=std::make_shared<::HaloDesktop::Playback::UpNextResolver>(m_apiClient,m_services.SettingsSync,m_services.Downloads);
        m_services.WindowPresentation = std::make_shared<::HaloDesktop::Shell::WindowPresentationService>();
        m_services.LayoutMetrics = std::make_shared<::HaloDesktop::Shell::LayoutMetricsService>();

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
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
        if (m_sessionService->IsSignedIn())
        {
            static_cast<void>(m_sessionService->RefreshIdentityAsync());
        }
    }


}
