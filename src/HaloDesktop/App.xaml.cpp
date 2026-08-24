#include "pch.h"
#include "App.xaml.h"
#include "Api/ApiClient.h"
#include "Api/HttpExecutor.h"
#include "Config/ServerConfig.h"
#include "Playback/MpvEngine.h"
#include "Playback/NullEngine.h"
#include "Services/MockDownloadService.h"
#include "Services/MockServices.h"
#include "Services/Auth/LocalAuthSession.h"
#include "Services/Auth/OidcAuthSession.h"
#include "Services/Auth/OidcSignInFlow.h"
#include "Services/Auth/SessionController.h"
#include "Services/Auth/SessionStore.h"
#include "Services/NavigationService.h"
#include "Services/QueryCache.h"
#include "Services/SessionService.h"
#include "Services/ThemeService.h"
#include "Shell/MainWindow.xaml.h"
#include "Shell/WindowPresentationService.h"

#include <memory>

namespace winrt::HaloDesktop::implementation
{
    App::App()
    {
        m_httpExecutor = std::make_shared<::HaloDesktop::Api::HttpExecutor>();
        m_queryCache = std::make_shared<::HaloDesktop::Services::QueryCache>();
        m_services.Navigation = std::make_shared<::HaloDesktop::Services::NavigationService>();
        m_sessionStore = std::make_shared<::HaloDesktop::Services::Auth::SessionStore>();
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

        m_services.Catalog = std::make_shared<::HaloDesktop::Services::MockCatalogService>();
        m_services.Metadata = std::make_shared<::HaloDesktop::Services::MockMetadataService>();
        m_services.Sources = std::make_shared<::HaloDesktop::Services::MockSourceService>();
        m_services.Downloads = std::make_shared<::HaloDesktop::Services::MockDownloadService>();
        m_services.Addons = std::make_shared<::HaloDesktop::Services::MockAddonService>();
        m_services.Session = m_sessionService;
        m_services.Theme = std::make_shared<::HaloDesktop::Services::ThemeService>();
#if defined(_M_X64) && !defined(HALO_USE_NULL_PLAYBACK)
        m_services.Playback = std::make_shared<::HaloDesktop::Playback::MpvEngine>();
#else
        m_services.Playback = std::make_shared<::HaloDesktop::Playback::NullEngine>();
#endif
        m_services.WindowPresentation = std::make_shared<::HaloDesktop::Shell::WindowPresentationService>();

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

    void App::OnLaunched([[maybe_unused]] Microsoft::UI::Xaml::LaunchActivatedEventArgs const& e)
    {
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
