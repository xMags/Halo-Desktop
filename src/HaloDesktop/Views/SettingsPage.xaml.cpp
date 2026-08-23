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
    void SettingsPage::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_viewModel.Refresh(); }
    void SettingsPage::OnSwitchServerClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_viewModel.SwitchServer(); }
    void SettingsPage::OnSignOutClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_viewModel.SignOut(); }
}
