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
        [[nodiscard]] Microsoft::UI::Xaml::Controls::Frame OverlayFrameControl() const;
        void PrepareForWindowClose() noexcept;
        void UpdateCaptionButtonColors();

        std::unique_ptr<::HaloDesktop::Shell::WindowSizing> m_windowSizing;
        winrt::event_token m_appWindowClosingToken{};
        Microsoft::UI::Xaml::FrameworkElement::ActualThemeChanged_revoker m_themeChangedRevoker{};
        winrt::event_token m_closedToken{};
        bool m_closePrepared{};
    };
} // namespace winrt::HaloDesktop::implementation

namespace winrt::HaloDesktop::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
} // namespace winrt::HaloDesktop::factory_implementation
