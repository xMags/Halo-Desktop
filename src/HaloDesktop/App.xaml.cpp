#include "pch.h"
#include "App.xaml.h"
#include "Playback/MpvEngine.h"
#include "Playback/NullEngine.h"
#include "Services/MockDownloadService.h"
#include "Services/MockServices.h"
#include "Services/NavigationService.h"
#include "Services/SessionService.h"
#include "Services/ThemeService.h"
#include "Shell/MainWindow.xaml.h"
#include "Shell/WindowPresentationService.h"

#include <memory>

namespace winrt::HaloDesktop::implementation
{
    App::App()
    {
        m_services.Catalog = std::make_shared<::HaloDesktop::Services::MockCatalogService>();
        m_services.Metadata = std::make_shared<::HaloDesktop::Services::MockMetadataService>();
        m_services.Sources = std::make_shared<::HaloDesktop::Services::MockSourceService>();
        m_services.Downloads = std::make_shared<::HaloDesktop::Services::MockDownloadService>();
        m_services.Addons = std::make_shared<::HaloDesktop::Services::MockAddonService>();
        m_services.Session = std::make_shared<::HaloDesktop::Services::SessionService>();
        m_services.Navigation = std::make_shared<::HaloDesktop::Services::NavigationService>();
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
        m_window = winrt::make<MainWindow>();
        m_window.Activate();
    }
}
