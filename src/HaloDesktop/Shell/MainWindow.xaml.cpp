#include "pch.h"
#include "Shell/MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "App.xaml.h"
#include "Playback/IPlaybackEngine.h"
#include "Services/ServiceInterfaces.h"
#include "Shell/WindowPresentationService.h"

#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.h>

namespace
{
    winrt::hstring CrumbForPage(HaloDesktop::Services::Page page)
    {
        using HaloDesktop::Services::Page;

        switch (page)
        {
        case Page::Home:
            return L"· Home";
        case Page::Search:
            return L"· Search";
        case Page::Library:
            return L"· Library";
        case Page::Detail:
            return L"· Northwind Divide";
        case Page::Sources:
            return L"· Sources";
        case Page::Downloads:
            return L"· Downloads";
        case Page::Settings:
            return L"· Settings";
        case Page::Connect:
        case Page::Login:
            return L"";
        case Page::Player:
            return L"· Northwind Divide · S02E04";
        }

        return L"";
    }
}

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
        UpdateCaptionButtonColors();
        App::Services().Downloads->Start();
        App::Services().Navigation->AttachOverlayFrame(OverlayFrameControl());
        App::Services().WindowPresentation->Attach(
            m_windowSizing->WindowHandle(),
            m_windowSizing->AppWindow(),
            RootGridControl().FindName(L"TitleBarRow").as<Microsoft::UI::Xaml::Controls::RowDefinition>());

        m_appWindowClosingToken = m_windowSizing->AppWindow().Closing(
            [weak = get_weak()](
                [[maybe_unused]] Microsoft::UI::Windowing::AppWindow const& window,
                [[maybe_unused]] Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args)
            {
                if (auto const self = weak.get())
                {
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

        App::Services().Navigation->SetRouteChangedHandler(
            [weak = get_weak()](::HaloDesktop::Services::Page page)
            {
                if (auto const self = weak.get())
                {
                    self->OnRouteChanged(page);
                }
            });

        if (!App::Services().Session->IsSignedIn())
        {
            auto const firstPage = App::Services().Session->ServerUrl().empty()
                ? ::HaloDesktop::Services::Page::Connect
                : ::HaloDesktop::Services::Page::Login;
            App::Services().Navigation->ShowOverlay(firstPage);
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

    void MainWindow::OnRouteChanged(::HaloDesktop::Services::Page page)
    {
        AppTitleBarControl().Crumb(CrumbForPage(page));
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
