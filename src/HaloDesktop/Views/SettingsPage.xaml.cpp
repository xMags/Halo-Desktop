#include "pch.h"
#include "Views/SettingsPage.xaml.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

#include "App.xaml.h"
#include "ViewModels/SettingsViewModel.h"

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
        m_loaded = true;
    }
    void SettingsPage::OnUnloaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_loaded = false; }
    void SettingsPage::OnAddonsRailClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ScrollTo(L"AddonsSection"); }
    void SettingsPage::OnPlaybackRailClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ScrollTo(L"PlaybackSection"); }
    void SettingsPage::OnSubtitlesRailClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ScrollTo(L"SubtitlesSection"); }
    void SettingsPage::OnAccountRailClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ScrollTo(L"AccountSection"); }
    void SettingsPage::OnAddAddonClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.AddAddon(FindName(L"AddonUrlBox").as<Microsoft::UI::Xaml::Controls::TextBox>().Text());
    }
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
    void SettingsPage::OnHeavyOutlineClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetOutline(2); }
    void SettingsPage::OnSwitchServerClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SwitchServer(); }
    void SettingsPage::OnSignOutClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SignOut(); }
    void SettingsPage::ScrollTo(wchar_t const* elementName)
    {
        FindName(elementName).as<Microsoft::UI::Xaml::FrameworkElement>().StartBringIntoView();
    }
    winrt::fire_and_forget SettingsPage::ShowDeleteAddonDialog(winrt::hstring name)
    {
        auto lifetime = get_strong();
        Microsoft::UI::Xaml::Controls::ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(winrt::box_value(L"Remove addon?"));
        dialog.Content(winrt::box_value(L"This removes the addon from the current mock session."));
        dialog.PrimaryButtonText(L"Remove");
        dialog.CloseButtonText(L"Cancel");
        if (co_await dialog.ShowAsync() == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            m_viewModel.RemoveAddon(name);
        }
    }
}
