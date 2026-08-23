#include "pch.h"
#include "App.xaml.h"
#include "Services/NavigationService.h"
#include "Shell/MainWindow.xaml.h"

#include <memory>

namespace winrt::HaloDesktop::implementation
{
    App::App()
    {
        m_services.Navigation = std::make_shared<::HaloDesktop::Services::NavigationService>();

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
