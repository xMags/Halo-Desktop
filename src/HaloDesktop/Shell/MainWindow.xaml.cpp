#include "pch.h"
#include "Shell/MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "App.xaml.h"
#include "Controls/ThemeColorLookup.h"
#include "Playback/IPlaybackEngine.h"
#include "Services/DevicePreferencesStore.h"
#include "Services/DiscordPresence.h"
#include "Views/PlayerPage.xaml.h"
#include "Services/ServiceInterfaces.h"
#include "Services/ThemeService.h"
#include "Shell/ShellChromePolicy.h"
#include "Shell/WindowBackdropService.h"
#include "Shell/WindowPresentationService.h"

#include <winrt/Windows.UI.h>

namespace winrt::HaloDesktop::implementation
{
    MainWindow::MainWindow() = default;

    void MainWindow::InitializeComponent()
    {
        MainWindowT::InitializeComponent();

        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBarControl());

        m_windowSizing = std::make_unique<::HaloDesktop::Shell::WindowSizing>(*this);
        // Where and how big it was left, before anything is shown.
        m_windowSizing->Restore(App::Services().DevicePreferences->WindowPlacement());
        // Before App activates the window: it stays off screen until its first
        // frame has rendered, or the fallback passes.
        m_reveal = ::HaloDesktop::Shell::WindowReveal::Begin(
            m_windowSizing->WindowHandle(),
            RootGridControl(),
            DispatcherQueue());
        // Applied before the first paint so a stored preference never flashes the other theme.
        App::Services().Theme->Attach(RootGridControl());
        // After the theme, so the first tint is the stored theme's ground colour.
        App::Services().WindowBackdrop->Attach(*this, RootGridControl(), WindowGround());
        UpdateCaptionButtonColors();
        App::Services().Downloads->Start();
        try
        {
            m_downloadAttention = std::make_shared<::HaloDesktop::Shell::DownloadAttentionPresenter>(
                reinterpret_cast<HWND>(m_windowSizing->WindowHandle()),
                App::Services().Downloads,
                App::Services().Navigation,
                DispatcherQueue());
            m_downloadAttention->Attach();
        }
        catch (...)
        {
            // The taskbar and notifications are extras; the app runs without them.
            m_downloadAttention.reset();
        }
        App::Services().Navigation->AttachOverlayFrame(OverlayFrameControl());
        App::Services().Navigation->AttachSheetFrame(SheetFrameControl());
        // Raised after every shell navigation and every overlay opening or closing,
        // with the navigation state already updated. Cleared by Navigation's Detach.
        App::Services().Navigation->SetRouteChangedHandler(
            [weak = get_weak()]([[maybe_unused]] ::HaloDesktop::Services::Page page)
            {
                if (auto const self = weak.get())
                {
                    self->ApplyShellChrome();
                }
            });
        App::Services().WindowPresentation->Attach(
            m_windowSizing->WindowHandle(),
            RootGridControl().FindName(L"TitleBarRow").as<Microsoft::UI::Xaml::Controls::RowDefinition>());
        m_activatedToken = Activated(
            [weak = get_weak()](
                [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                Microsoft::UI::Xaml::WindowActivatedEventArgs const& args)
            {
                auto const deactivated =
                    args.WindowActivationState() == Microsoft::UI::Xaml::WindowActivationState::Deactivated;
                App::Services().WindowPresentation->SetWindowActivation(
                    deactivated ? ::HaloDesktop::Shell::WindowActivation::Inactive
                                : ::HaloDesktop::Shell::WindowActivation::Active);
                App::Services().WindowBackdrop->SetWindowActive(!deactivated);
                if (deactivated)
                {
                    return;
                }

                auto const self = weak.get();
                if (!self)
                {
                    return;
                }
                // The first activation shows the window; one left maximized
                // goes back to maximized now (once, then never again).
                self->m_windowSizing->ApplyPendingMaximize();

                auto const page = self->OverlayFrameControl()
                    .Content()
                    .try_as<winrt::HaloDesktop::PlayerPage>();
                if (!page)
                {
                    return;
                }
                winrt::get_self<PlayerPage>(page)->RestoreOverlayFocusAfterActivation();
            });

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
                    App::Services().WindowBackdrop->RefreshTheme();
                }
            });

        if (App::Services().Session->Mode() == ::HaloDesktop::Services::SessionMode::SignedOut)
        {
            App::Services().Navigation->ShowOverlay(::HaloDesktop::Services::Page::Login);
        }
        ApplyShellChrome();

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

    Microsoft::UI::Xaml::Controls::Frame MainWindow::SheetFrameControl() const
    {
        return RootGridControl().FindName(L"SheetFrame").as<Microsoft::UI::Xaml::Controls::Frame>();
    }

    void MainWindow::PrepareForWindowClose() noexcept
    {
        if (m_closePrepared)
        {
            return;
        }
        m_closePrepared = true;

        RememberWindowPlacement();

        try
        {
            App::Services().DiscordPresence->Clear();
            App::Services().Playback->Stop();
            App::Services().Playback->DetachVideoSurface();
            if (m_downloadAttention)
            {
                m_downloadAttention->Detach();
                m_downloadAttention.reset();
            }
            App::Services().Downloads->Stop();
            App::Services().Navigation->Detach();
            if (m_activatedToken.value != 0)
            {
                Activated(m_activatedToken);
                m_activatedToken = {};
            }
            App::Services().WindowPresentation->Detach();
            m_themeChangedRevoker.revoke();
            // The backdrop lets go of the window while it is still whole.
            App::Services().WindowBackdrop->Detach();
            // The service holds the root it themes and does not own it; it lets
            // go here, before the content below is torn down.
            App::Services().Theme->Detach();
            m_reveal.reset();

            // Generated x:Name fields are strong references. Clear them while
            // XAML is alive so Frame navigation caches do not release Pages
            // during WindowsXamlManager shutdown.
            Content(nullptr);
            OverlayFrame(nullptr);
            SheetFrame(nullptr);
            ShellLayer(nullptr);
            PlayerTitleBarGround(nullptr);
            WindowGround(nullptr);
            AppTitleBar(nullptr);
            TitleBarRow(nullptr);
            RootGrid(nullptr);
        }
        catch (...)
        {
        }
    }

    // Saved as the window closes. A player still full screen says nothing about
    // the window, so the last placement saved stays.
    void MainWindow::RememberWindowPlacement() noexcept
    {
        try
        {
            if (App::Services().WindowPresentation->IsFullscreen())
            {
                return;
            }
            if (auto const placement = m_windowSizing->CurrentPlacement())
            {
                App::Services().DevicePreferences->WindowPlacement(*placement);
            }
        }
        catch (...)
        {
            OutputDebugStringW(L"Halo could not save the window's size and position.\n");
        }
    }

    winrt::fire_and_forget MainWindow::FinishOrderedPlayerClose()
    {
        auto lifetime=get_strong();
        try
        {
            try{if(auto page=OverlayFrameControl().Content().try_as<winrt::HaloDesktop::PlayerPage>())co_await winrt::get_self<PlayerPage>(page)->PrepareForWindowCloseAsync();}catch(...){}
            m_windowCloseApproved=true;m_playerClosePending=false;Close();
        }
        catch(...)
        {
        }
    }

    void MainWindow::ApplyShellChrome()
    {
        using ::HaloDesktop::Services::Page;

        auto const& navigation = *App::Services().Navigation;
        auto const overlayOpen = navigation.IsOverlayOpen();
        auto const chrome = ::HaloDesktop::Shell::ResolveShellChrome({
            .TitlePageCurrent = navigation.CurrentPage() == Page::Detail,
            .SignInOverlayOpen = overlayOpen && navigation.CurrentOverlayPage() == Page::Login,
            .PlayerOverlayOpen = overlayOpen && navigation.CurrentOverlayPage() == Page::Player,
        });

        // The root's theme change raises ActualThemeChanged, which repaints the
        // caption buttons; the player bar is set first so that repaint sees it.
        m_playerTitleBar = chrome.PlayerTitleBar;
        PlayerTitleBarGround().Visibility(chrome.PlayerTitleBar
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed);
        AppTitleBarControl().RequestedTheme(chrome.PlayerTitleBar
            ? Microsoft::UI::Xaml::ElementTheme::Dark
            : Microsoft::UI::Xaml::ElementTheme::Default);
        auto const overlay = OverlayFrameControl();
        Microsoft::UI::Xaml::Controls::Grid::SetRow(overlay, chrome.OverlayCoversTitleBar ? 0 : 1);
        Microsoft::UI::Xaml::Controls::Grid::SetRowSpan(overlay, chrome.OverlayCoversTitleBar ? 2 : 1);
        App::Services().Theme->SetForcedDarkScope(chrome.ForceDarkPalette);
        App::Services().WindowBackdrop->SetPlayerShowing(overlayOpen && navigation.CurrentOverlayPage() == Page::Player);
        UpdateCaptionButtonColors();
    }

    void MainWindow::UpdateCaptionButtonColors()
    {
        auto const titleBar = m_windowSizing->AppWindow().TitleBar();
        auto const transparent = winrt::Windows::UI::Colors::Transparent();
        // Over the player's near-black bar the buttons take the dark palette in
        // both themes; a title page has already forced the root dark.
        auto const theme = m_playerTitleBar
                || RootGridControl().ActualTheme() != Microsoft::UI::Xaml::ElementTheme::Light
            ? Microsoft::UI::Xaml::ElementTheme::Dark
            : Microsoft::UI::Xaml::ElementTheme::Light;
        auto const fallback = theme == Microsoft::UI::Xaml::ElementTheme::Dark
            ? winrt::Windows::UI::Colors::White()
            : winrt::Windows::UI::Colors::Black();
        // The reference's caption buttons: the secondary text colour at rest and
        // the primary one under the pointer.
        auto const rest = ::HaloDesktop::Controls::ThemeColor(theme, L"HaloText2Color").value_or(fallback);
        auto const hover = ::HaloDesktop::Controls::ThemeColor(theme, L"HaloText1Color").value_or(fallback);

        titleBar.ButtonBackgroundColor(transparent);
        titleBar.ButtonInactiveBackgroundColor(transparent);
        titleBar.ButtonForegroundColor(rest);
        titleBar.ButtonInactiveForegroundColor(rest);
        titleBar.ButtonHoverForegroundColor(hover);
        titleBar.ButtonPressedForegroundColor(hover);
    }
}
