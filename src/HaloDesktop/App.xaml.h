#pragma once

#include "App.xaml.g.h"

#include "Services/AppServices.h"

#include <memory>

namespace HaloDesktop::Api
{
    class ApiClient;
    class HttpExecutor;
}

namespace HaloDesktop::Services
{
    class DownloadService;
    class DevicePreferencesStore;
    class PlaybackPreferences;
    class QueryCache;
    class SessionService;
}

namespace HaloDesktop::Services::Auth
{
    class LocalAuthSession;
    class OidcAuthSession;
    class OidcSignInFlow;
    class SessionController;
    class SessionStore;
}

namespace HaloDesktop::Services::Downloads
{
    class TransferEngine;
}

namespace HaloDesktop::Storage
{
    class AppStoragePaths;
}

namespace winrt::HaloDesktop::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
        static ::HaloDesktop::Services::AppServices& Services();
        static Microsoft::UI::Xaml::Window Window();

    private:
        winrt::Windows::Foundation::IAsyncAction LaunchAsync();

        std::shared_ptr<::HaloDesktop::Api::HttpExecutor> m_httpExecutor;
        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::shared_ptr<::HaloDesktop::Storage::AppStoragePaths> m_storagePaths;
        std::shared_ptr<::HaloDesktop::Services::DevicePreferencesStore> m_devicePreferences;
        std::shared_ptr<::HaloDesktop::Services::PlaybackPreferences> m_playbackPreferences;
        std::shared_ptr<::HaloDesktop::Services::QueryCache> m_queryCache;
        std::shared_ptr<::HaloDesktop::Services::Auth::SessionStore> m_sessionStore;
        std::shared_ptr<::HaloDesktop::Services::Auth::LocalAuthSession> m_localAuthSession;
        std::shared_ptr<::HaloDesktop::Services::Auth::OidcAuthSession> m_oidcAuthSession;
        std::shared_ptr<::HaloDesktop::Services::Auth::OidcSignInFlow> m_oidcSignInFlow;
        std::shared_ptr<::HaloDesktop::Services::Auth::SessionController> m_sessionController;
        std::shared_ptr<::HaloDesktop::Services::SessionService> m_sessionService;
        std::shared_ptr<::HaloDesktop::Services::Downloads::TransferEngine> m_downloadEngine;
        std::shared_ptr<::HaloDesktop::Services::DownloadService> m_downloadService;
        ::HaloDesktop::Services::AppServices m_services;
        winrt::Microsoft::UI::Xaml::Window m_window{ nullptr };
    };
}
