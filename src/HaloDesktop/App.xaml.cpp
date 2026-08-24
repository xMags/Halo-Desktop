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
#include "Services/MockDownloadService.h"
#include "Services/AddonService.h"
#include "Services/CatalogService.h"
#include "Services/Auth/LocalAuthSession.h"
#include "Services/Auth/OidcAuthSession.h"
#include "Services/Auth/OidcSignInFlow.h"
#include "Services/Auth/SessionController.h"
#include "Services/Auth/SessionStore.h"
#include "Services/NavigationService.h"
#include "Services/LibraryService.h"
#include "Services/MetadataService.h"
#include "Services/QueryCache.h"
#include "Services/SessionService.h"
#include "Services/SettingsSyncService.h"
#include "Services/SourceService.h"
#include "Services/WatchStateService.h"
#include "Services/ThemeService.h"
#if defined(_DEBUG)
#include "Services/Downloads/TransferEngine.h"
#endif
#include "Shell/MainWindow.xaml.h"
#include "Shell/WindowPresentationService.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>

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

        auto addonService = std::make_shared<::HaloDesktop::Services::AddonService>(
            m_apiClient,
            m_queryCache,
            m_sessionService);
        m_services.Addons = addonService;
        m_services.Library = std::make_shared<::HaloDesktop::Services::LibraryService>(m_apiClient);
        m_services.WatchState = std::make_shared<::HaloDesktop::Services::WatchStateService>(m_apiClient);
        auto catalogService = std::make_shared<::HaloDesktop::Services::CatalogService>(
            m_apiClient,
            addonService,
            m_services.Library,
            m_services.WatchState);
        m_services.Catalog = catalogService;
        m_services.Downloads = std::make_shared<::HaloDesktop::Services::MockDownloadService>();
        m_services.Metadata = std::make_shared<::HaloDesktop::Services::MetadataService>(m_apiClient,m_services.WatchState);
        m_services.Sources = std::make_shared<::HaloDesktop::Services::SourceService>(m_apiClient,m_services.Downloads);
        m_services.SettingsSync = std::make_shared<::HaloDesktop::Services::SettingsSyncService>(
            m_apiClient,
            m_queryCache,
            Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread());
        m_services.Session = m_sessionService;
        m_services.Theme = std::make_shared<::HaloDesktop::Services::ThemeService>();
#if defined(_M_X64) && !defined(HALO_USE_NULL_PLAYBACK)
        m_services.Playback = std::make_shared<::HaloDesktop::Playback::MpvEngine>();
#else
        m_services.Playback = std::make_shared<::HaloDesktop::Playback::NullEngine>();
#endif
        m_services.Subtitles=std::make_shared<::HaloDesktop::Playback::SubtitleController>(m_apiClient,m_services.Playback,m_services.SettingsSync);
        m_services.UpNext=std::make_shared<::HaloDesktop::Playback::UpNextResolver>(m_apiClient,m_services.SettingsSync);
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

    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& e)
    {
#if defined(_DEBUG)
        static_cast<void>(e);
        std::wstring_view const commandLine{ GetCommandLineW() };
        auto const probe = commandLine.find(L"halo-download-probe\t");
        if (probe != std::wstring_view::npos)
        {
            StartDownloadProbe(winrt::hstring{ commandLine.substr(probe) });
        }
#else
        static_cast<void>(e);
#endif
        static_cast<void>(LaunchAsync());
    }

#if defined(_DEBUG)
    void App::StartDownloadProbe(winrt::hstring const& arguments)
    {
        constexpr std::wstring_view prefix = L"halo-download-probe\t";
        std::wstring_view remaining{ arguments };
        if (!remaining.starts_with(prefix))
        {
            return;
        }
        remaining.remove_prefix(prefix.size());
        std::array<std::wstring_view, 4> fields{};
        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            auto const separator = remaining.find(L'\t');
            if (index + 1 == fields.size())
            {
                fields[index] = remaining;
                remaining = {};
                break;
            }
            if (separator == std::wstring_view::npos)
            {
                return;
            }
            fields[index] = remaining.substr(0, separator);
            remaining.remove_prefix(separator + 1);
        }
        auto const mode = fields[0];
        auto const dataRoot = std::filesystem::path{ fields[1] };
        auto const source = std::wstring{ fields[2] };
        std::uint64_t size{};
        int stage = 1;
        auto const resultPath = dataRoot / L"probe-result.txt";
        auto writeResult = [&resultPath](std::string const& value) noexcept
        {
            try
            {
                std::ofstream output(resultPath, std::ios::binary | std::ios::trunc);
                output << value;
            }
            catch (...)
            {
            }
        };
        try
        {
            size = std::stoull(std::wstring{ fields[3] });
            ::HaloDesktop::Services::Downloads::RunDownloadEngineUnitChecks();
            stage = 2;
            m_downloadProbe = std::make_shared<::HaloDesktop::Services::Downloads::TransferEngine>(dataRoot);
            stage = 3;
            m_downloadProbe->SetAccount(L"http://fixture.invalid", L"download-probe-user");
            stage = 4;
            if (mode == L"pause")
            {
                auto const records = m_downloadProbe->List();
                if (!records.empty())
                {
                    m_downloadProbe->Pause(records.front().JobId);
                }
            }
            if (mode == L"start" && m_downloadProbe->List().empty())
            {
                ::HaloDesktop::Services::Downloads::DownloadMedia media{
                    .VideoId = L"probe:large",
                    .ItemId = L"probe:large",
                    .MediaType = L"movie",
                    .Title = L"Large transfer probe",
                    .FileName = L"large-probe.mkv",
                    .VideoSize = size,
                };
                ::HaloDesktop::Services::Downloads::DownloadStartRequest request{
                    .Media = std::move(media),
                    .Request = ::HaloDesktop::Services::Downloads::ProtectedRequest{ .Url = source },
                };
                static_cast<void>(m_downloadProbe->Start(std::move(request)));
            }
            writeResult("ready");
        }
        catch (...)
        {
            writeResult("failed-stage-" + std::to_string(stage) + "-hr-" + std::to_string(winrt::to_hresult()));
            m_downloadProbe.reset();
        }
    }
#endif

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
