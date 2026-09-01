#include "pch.h"
#include "Views/SettingsPage.xaml.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Views/PageDialog.h"
#include "ViewModels/SettingsRailPolicy.h"
#include "ViewModels/SettingsViewModel.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <winrt/Windows.System.h>

namespace
{
    // The client's own repository. Releases are where a newer build would appear, so
    // that is the page the card offers rather than the repository root.
    constexpr wchar_t const* kReleasesUrl = L"https://github.com/xMags/Halo-Desktop/releases";

    // The rail entries in document order, paired with the section each one points at
    // and the visual state that lights it. The three arrays are indexed together, so
    // they must stay in the same order as the rail itself.
    constexpr std::array<wchar_t const*, 5> kSectionNames{
        L"AppearanceSection",
        L"AddonsSection",
        L"PlaybackSection",
        L"SubtitlesSection",
        L"AccountSection",
    };
    constexpr std::array<wchar_t const*, 5> kRailStates{
        L"RailAppearance",
        L"RailAddons",
        L"RailPlayback",
        L"RailSubtitles",
        L"RailAccount",
    };

    // How far below the top edge a section has to reach before the rail counts it as
    // the one being read. A band rather than the edge itself, so a boundary resting on
    // the top of the viewport does not flip the selection back and forth.
    constexpr double kAnchorFraction = 0.25;
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
        // The rail follows the scroll position from here on, but the page opens at the
        // top, so light the first entry before anything has scrolled.
        UpdateRailSelection();
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
    void SettingsPage::OnDefaultFontClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFont(0); }
    void SettingsPage::OnSystemFontClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFont(1); }
    void SettingsPage::OnSerifFontClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFont(2); }
    void SettingsPage::OnMonoFontClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFont(3); }
    void SettingsPage::OnNoOutlineClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetOutline(0); }
    void SettingsPage::OnThinOutlineClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetOutline(1); }
    void SettingsPage::OnNormalOutlineClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetOutline(2); }
    void SettingsPage::OnHeavyOutlineClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetOutline(3); }
    void SettingsPage::OnCheckForUpdatesClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        OpenReleasesPageAsync();
    }

    winrt::fire_and_forget SettingsPage::OpenReleasesPageAsync()
    {
        auto lifetime = get_strong();
        try
        {
            bool launched{};
            try
            {
                launched = co_await winrt::Windows::System::Launcher::LaunchUriAsync(
                    winrt::Windows::Foundation::Uri{ kReleasesUrl });
            }
            catch (...)
            {
            }
            if (launched)
            {
                co_return;
            }
            auto const xamlRoot = XamlRoot();
            if (!xamlRoot)
            {
                co_return;
            }
            auto dialog = ::HaloDesktop::Views::MakeDialog(
                xamlRoot,
                ActualTheme(),
                L"Could not open the releases page",
                L"Check your default browser settings and try again.");
            dialog.CloseButtonText(L"Close");
            co_await dialog.ShowAsync();
        }
        catch (...)
        {
        }
    }
    void SettingsPage::OnSignOutClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SignOut(); }
    void SettingsPage::ScrollTo(wchar_t const* elementName)
    {
        FindName(elementName).as<Microsoft::UI::Xaml::FrameworkElement>().StartBringIntoView();
    }
    void SettingsPage::OnFormViewChanged([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const&)
    {
        UpdateRailSelection();
    }
    void SettingsPage::OnFormSizeChanged([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::SizeChangedEventArgs const&)
    {
        // Sections shift when the addon list finishes loading, which moves the reader
        // to a different section without any scrolling having happened.
        UpdateRailSelection();
    }
    void SettingsPage::UpdateRailSelection()
    {
        if (!m_loaded)
        {
            return;
        }
        auto const scroller = SettingsScroller();
        auto const viewport = scroller.ViewportHeight();
        if (viewport <= 0.0)
        {
            return;
        }
        std::array<double, kSectionNames.size()> tops{};
        for (std::size_t index = 0; index < kSectionNames.size(); ++index)
        {
            auto const section = FindName(kSectionNames[index]).try_as<Microsoft::UI::Xaml::FrameworkElement>();
            if (!section)
            {
                return;
            }
            tops[index] = section.TransformToVisual(scroller).TransformPoint({ 0.0f, 0.0f }).Y;
        }
        // Only treat the view as ended when there was somewhere to scroll to, otherwise
        // a form short enough to fit would sit permanently on its last section.
        auto const scrollable = scroller.ScrollableHeight();
        auto const atEnd = scrollable > 0.5 && scroller.VerticalOffset() >= scrollable - 1.0;
        auto const active = ::HaloDesktop::ViewModels::ActiveSettingsSection(tops, viewport * kAnchorFraction, atEnd);
        if (m_railApplied && active == m_activeRail)
        {
            return;
        }
        m_activeRail = active;
        m_railApplied = true;
        Microsoft::UI::Xaml::VisualStateManager::GoToState(*this, kRailStates[active], false);
    }
    winrt::fire_and_forget SettingsPage::ShowDeleteAddonDialog(winrt::hstring id)
    {
        auto lifetime = get_strong();
        try
        {
            auto const viewModel = winrt::get_self<SettingsViewModel>(m_viewModel);
            auto dialog = ::HaloDesktop::Views::MakeDialog(
                XamlRoot(),
                ActualTheme(),
                L"Remove addon?",
                viewModel->IsGlobalAddon(id)
                    ? L"This removes the addon for every user on this server."
                    : L"This removes the addon from your account.");
            dialog.PrimaryButtonText(L"Remove");
            dialog.CloseButtonText(L"Cancel");
            if (co_await dialog.ShowAsync() == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
            {
                m_viewModel.RemoveAddon(id);
            }
        }
        catch (...)
        {
        }
    }
}
