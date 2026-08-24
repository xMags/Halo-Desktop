#include "pch.h"
#include "Shell/MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "App.xaml.h"
#include "Playback/IPlaybackEngine.h"
#include "Views/PlayerPage.xaml.h"
#include "Services/ServiceInterfaces.h"
#include "Services/ThemeService.h"
#include "Shell/WindowPresentationService.h"

#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.h>

namespace winrt::HaloDesktop::implementation
{
    MainWindow::MainWindow() = default;

    void MainWindow::InitializeComponent()
    {
        MainWindowT::InitializeComponent();

        SystemBackdrop(Microsoft::UI::Xaml::Media::MicaBackdrop{});
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBarControl());

        m_windowSizing = std::make_unique<::HaloDesktop::Shell::WindowSizing>(*this);
        // Applied before the first paint so a stored preference never flashes the other theme.
        App::Services().Theme->Attach(RootGridControl());
        UpdateCaptionButtonColors();
        App::Services().Downloads->Start();
        App::Services().Navigation->AttachOverlayFrame(OverlayFrameControl());
        App::Services().WindowPresentation->Attach(
            m_windowSizing->WindowHandle(),
            RootGridControl().FindName(L"TitleBarRow").as<Microsoft::UI::Xaml::Controls::RowDefinition>());

        m_appWindowClosingToken = m_windowSizing->AppWindow().Closing(
            [weak = get_weak()](
                [[maybe_unused]] Microsoft::UI::Windowing::AppWindow const& window,
                [[maybe_unused]] Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args)
            {
                if (auto const self = weak.get())
                {
                    if(!self->m_windowCloseApproved)
                    {
                        if(self->m_playerClosePending){args.Cancel(true);return;}
                        if(self->OverlayFrameControl().Content().try_as<winrt::HaloDesktop::PlayerPage>())
                        {
                            args.Cancel(true);self->m_playerClosePending=true;self->FinishOrderedPlayerClose();return;
                        }
                    }
                    self->PrepareForWindowClose();
                }
            });

        m_themeChangedRevoker = RootGridControl().ActualThemeChanged(
            winrt::auto_revoke,
            [weak = get_weak()](
                [[maybe_unused]] Microsoft::UI::Xaml::FrameworkElement const& sender,
                [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args)
            {
                if (auto const self = weak.get())
                {
                    self->UpdateCaptionButtonColors();
                }
            });

        if (!App::Services().Session->IsSignedIn())
        {
            App::Services().Navigation->ShowOverlay(::HaloDesktop::Services::Page::Login);
        }

        // An explicit token avoids revoking the Window.Closed event after its
        // native Window has already completed teardown.
        m_closedToken = Closed(
            [weak = get_weak()](
                [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                [[maybe_unused]] Microsoft::UI::Xaml::WindowEventArgs const& args)
            {
                if (auto const self = weak.get())
                {
                    self->PrepareForWindowClose();
                }
            });
    }

    Microsoft::UI::Xaml::Controls::Grid MainWindow::RootGridControl() const
    {
        return Content().as<Microsoft::UI::Xaml::Controls::Grid>();
    }

    winrt::HaloDesktop::TitleBar MainWindow::AppTitleBarControl() const
    {
        return RootGridControl().FindName(L"AppTitleBar").as<winrt::HaloDesktop::TitleBar>();
    }

    Microsoft::UI::Xaml::Controls::Frame MainWindow::OverlayFrameControl() const
    {
        return RootGridControl().FindName(L"OverlayFrame").as<Microsoft::UI::Xaml::Controls::Frame>();
    }

    void MainWindow::PrepareForWindowClose() noexcept
    {
        if (m_closePrepared)
        {
            return;
        }
        m_closePrepared = true;

        try
        {
            App::Services().Playback->Stop();
            App::Services().Playback->DetachVideoWindow();
            App::Services().Downloads->Stop();
            App::Services().Navigation->Detach();
            App::Services().WindowPresentation->Detach();
            m_themeChangedRevoker.revoke();

            // Generated x:Name fields are strong references. Clear them while
            // XAML is alive so Frame navigation caches do not release Pages
            // during WindowsXamlManager shutdown.
            Content(nullptr);
            OverlayFrame(nullptr);
            ShellLayer(nullptr);
            AppTitleBar(nullptr);
            TitleBarRow(nullptr);
            RootGrid(nullptr);
        }
        catch (...)
        {
        }
    }

    winrt::fire_and_forget MainWindow::FinishOrderedPlayerClose()
    {
        auto lifetime=get_strong();
        try{if(auto page=OverlayFrameControl().Content().try_as<winrt::HaloDesktop::PlayerPage>())co_await winrt::get_self<PlayerPage>(page)->PrepareForWindowCloseAsync();}catch(...){}
        m_windowCloseApproved=true;m_playerClosePending=false;m_windowSizing->AppWindow().Destroy();
    }

    void MainWindow::UpdateCaptionButtonColors()
    {
        auto const titleBar = m_windowSizing->AppWindow().TitleBar();
        auto const transparent = winrt::Windows::UI::Colors::Transparent();
        auto const foreground = RootGridControl().ActualTheme() == Microsoft::UI::Xaml::ElementTheme::Light
            ? winrt::Windows::UI::Colors::Black()
            : winrt::Windows::UI::Colors::White();

        titleBar.ButtonBackgroundColor(transparent);
        titleBar.ButtonInactiveBackgroundColor(transparent);
        titleBar.ButtonForegroundColor(foreground);
        titleBar.ButtonInactiveForegroundColor(foreground);
    }
}
