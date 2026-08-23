#include "pch.h"
#include "Shell/MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "App.xaml.h"
#include "Services/ServiceInterfaces.h"

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

        m_closedRevoker = Closed(
            winrt::auto_revoke,
            [weak = get_weak()](
                [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                [[maybe_unused]] Microsoft::UI::Xaml::WindowEventArgs const& args)
            {
                if (auto const self = weak.get())
                {
                    App::Services().Downloads->Stop();
                    App::Services().Navigation->Detach();
                    self->m_themeChangedRevoker.revoke();
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
