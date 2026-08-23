#pragma once

#include "App.xaml.g.h"

#include "Services/AppServices.h"

namespace winrt::HaloDesktop::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
        static ::HaloDesktop::Services::AppServices& Services();

    private:
        ::HaloDesktop::Services::AppServices m_services;
        winrt::Microsoft::UI::Xaml::Window m_window{ nullptr };
    };
}
