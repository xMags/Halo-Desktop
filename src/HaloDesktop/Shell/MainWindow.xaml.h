#pragma once

#include "MainWindow.g.h"

#include "Services/NavigationService.h"
#include "Shell/WindowSizing.h"

#include <memory>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        void InitializeComponent();

    private:
        [[nodiscard]] Microsoft::UI::Xaml::Controls::Grid RootGridControl() const;
        [[nodiscard]] winrt::HaloDesktop::TitleBar AppTitleBarControl() const;
        void OnRouteChanged(::HaloDesktop::Services::Page page);
        void UpdateCaptionButtonColors();

        std::unique_ptr<::HaloDesktop::Shell::WindowSizing> m_windowSizing;
        Microsoft::UI::Xaml::FrameworkElement::ActualThemeChanged_revoker m_themeChangedRevoker{};
        Microsoft::UI::Xaml::Window::Closed_revoker m_closedRevoker{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
