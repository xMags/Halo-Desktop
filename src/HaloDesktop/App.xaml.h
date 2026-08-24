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
    class QueryCache;
}

namespace winrt::HaloDesktop::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
        static ::HaloDesktop::Services::AppServices& Services();

    private:
        std::shared_ptr<::HaloDesktop::Api::HttpExecutor> m_httpExecutor;
        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::shared_ptr<::HaloDesktop::Services::QueryCache> m_queryCache;
        ::HaloDesktop::Services::AppServices m_services;
        winrt::Microsoft::UI::Xaml::Window m_window{ nullptr };
    };
}
