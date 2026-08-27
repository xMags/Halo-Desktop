#include "pch.h"
#include "Views/SettingsPage.xaml.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

#include "App.xaml.h"
#include "ViewModels/SettingsViewModel.h"

#include <chrono>
#include <winrt/Windows.System.h>

namespace
{
    // The client's own repository. Releases are where a newer build would appear, so
    // that is the page the card offers rather than the repository root.
    constexpr wchar_t const* kReleasesUrl = L"https://github.com/xMags/Halo-Desktop/releases";
}

namespace winrt::HaloDesktop::implementation
{
    SettingsPage::SettingsPage()
        : m_viewModel(winrt::make<SettingsViewModel>(App::Services()))
    {
    }
    winrt::HaloDesktop::SettingsViewModel SettingsPage::ViewModel() const { return m_viewModel; }
    void SettingsPage::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.Refresh();
        auto const viewModel = winrt::get_self<SettingsViewModel>(m_viewModel);
        FindName(L"AddonList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->AddonsView());
        if (!m_healthTimer)
        {
            m_healthTimer = DispatcherQueue().CreateTimer();
            m_healthTimer.Interval(std::chrono::seconds{ 30 });
            m_healthTimer.IsRepeating(true);
            m_healthTickRevoker = m_healthTimer.Tick(winrt::auto_revoke, [weak = get_weak()](auto const&, auto const&)
            {
                if (auto const self = weak.get())
                {
                    self->m_viewModel.ProbeHealth();
                }
            });
        }
        m_healthTimer.Start();
        m_loaded = true;
    }
    void SettingsPage::OnUnloaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_loaded = false;
        if (m_healthTimer)
        {
            m_healthTimer.Stop();
        }
        m_healthTickRevoker.revoke();
        m_healthTimer = nullptr;
        m_viewModel.CancelHealthProbe();
    }
    void SettingsPage::OnAppearanceRailClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ScrollTo(L"AppearanceSection"); }
    void SettingsPage::OnLightThemeClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetTheme(0); }
    void SettingsPage::OnDarkThemeClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetTheme(1); }
    void SettingsPage::OnSystemThemeClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetTheme(2); }
    void SettingsPage::OnAddonsRailClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ScrollTo(L"AddonsSection"); }
    void SettingsPage::OnPlaybackRailClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ScrollTo(L"PlaybackSection"); }
    void SettingsPage::OnSubtitlesRailClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ScrollTo(L"SubtitlesSection"); }
    void SettingsPage::OnAccountRailClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ScrollTo(L"AccountSection"); }
    void SettingsPage::OnAddAddonClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.AddAddon(FindName(L"AddonUrlBox").as<Microsoft::UI::Xaml::Controls::TextBox>().Text());
    }
    void SettingsPage::OnRetrySettingsClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Refresh(); }
    void SettingsPage::OnAddonToggled(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (!m_loaded) return;
        auto const toggle = sender.as<Microsoft::UI::Xaml::Controls::ToggleSwitch>();
        m_viewModel.ToggleAddon(winrt::unbox_value<winrt::hstring>(toggle.Tag()), toggle.IsOn());
    }
    void SettingsPage::OnDeleteAddonClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ShowDeleteAddonDialog(winrt::unbox_value<winrt::hstring>(sender.as<Microsoft::UI::Xaml::Controls::Button>().Tag()));
    }
    void SettingsPage::OnSystemFontClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFont(0); }
    void SettingsPage::OnSerifFontClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFont(1); }
    void SettingsPage::OnMonoFontClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFont(2); }
    void SettingsPage::OnNoOutlineClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetOutline(0); }
    void SettingsPage::OnThinOutlineClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetOutline(1); }
    void SettingsPage::OnNormalOutlineClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetOutline(2); }
    void SettingsPage::OnHeavyOutlineClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetOutline(3); }
    void SettingsPage::OnCheckForUpdatesClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        // The shell owns the launch and reports its own failure to the user, so the
        // operation is deliberately dropped rather than awaited on the UI thread.
        static_cast<void>(winrt::Windows::System::Launcher::LaunchUriAsync(
            winrt::Windows::Foundation::Uri{ kReleasesUrl }));
    }
    void SettingsPage::OnSignOutClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SignOut(); }
    void SettingsPage::ScrollTo(wchar_t const* elementName)
    {
        FindName(elementName).as<Microsoft::UI::Xaml::FrameworkElement>().StartBringIntoView();
    }
    winrt::fire_and_forget SettingsPage::ShowDeleteAddonDialog(winrt::hstring id)
    {
        auto lifetime = get_strong();
        Microsoft::UI::Xaml::Controls::ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(winrt::box_value(L"Remove addon?"));
        auto const viewModel = winrt::get_self<SettingsViewModel>(m_viewModel);
        dialog.Content(winrt::box_value(viewModel->IsGlobalAddon(id)
            ? L"This removes the addon for every user on this server."
            : L"This removes the addon from your account."));
        dialog.PrimaryButtonText(L"Remove");
        dialog.CloseButtonText(L"Cancel");
        if (co_await dialog.ShowAsync() == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            m_viewModel.RemoveAddon(id);
        }
    }
}
