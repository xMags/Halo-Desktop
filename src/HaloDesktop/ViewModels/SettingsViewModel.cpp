#include "pch.h"
#include "ViewModels/SettingsViewModel.h"
#if __has_include("AddonRowViewModel.g.cpp")
#include "AddonRowViewModel.g.cpp"
#endif
#if __has_include("SettingsViewModel.g.cpp")
#include "SettingsViewModel.g.cpp"
#endif

#include "Services/NavigationService.h"
#include "Services/SampleData.h"
#include "ViewModels/ObservableHelper.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>
#include <winrt/Windows.Foundation.h>

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
}

namespace winrt::HaloDesktop::implementation
{
    AddonRowViewModel::AddonRowViewModel(winrt::HaloDesktop::Addon addon) : m_addon(std::move(addon)) {}
    winrt::hstring AddonRowViewModel::Initials() const { return m_addon.Initials(); }
    winrt::hstring AddonRowViewModel::Name() const { return m_addon.Name(); }
    winrt::hstring AddonRowViewModel::Version() const { return m_addon.Version(); }
    winrt::hstring AddonRowViewModel::Scope() const { return m_addon.Scope(); }
    winrt::hstring AddonRowViewModel::Provides() const { return m_addon.Provides(); }
    bool AddonRowViewModel::Enabled() const noexcept { return m_addon.Enabled(); }
    Microsoft::UI::Xaml::Visibility AddonRowViewModel::GlobalVisibility() const noexcept { return m_addon.Scope() == L"GLOBAL" ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility AddonRowViewModel::YoursVisibility() const noexcept { return m_addon.Scope() == L"YOURS" ? Visible : Collapsed; }

    SettingsViewModel::SettingsViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_session(services.Session),
          m_theme(services.Theme),
          m_addonService(services.Addons),
          m_navigation(services.Navigation),
          m_addons(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        Refresh();
        SynchronizeAddons();
    }
    winrt::hstring SettingsViewModel::ServerUrl() const { return m_serverUrl; }
    winrt::hstring SettingsViewModel::UserName() const { return m_userName; }
    winrt::hstring SettingsViewModel::SignedInLine() const { return winrt::hstring(std::wstring(m_userName) + L" · admin"); }
    winrt::Windows::Foundation::IInspectable SettingsViewModel::Addons() const { return m_addons; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> SettingsViewModel::AddonsView() const { return m_addons; }
    winrt::hstring SettingsViewModel::AddonNoticeText() const { return m_addonNoticeText; }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::AddonNoticeVisibility() const noexcept { return m_addonNoticeVisible ? Visible : Collapsed; }
    double SettingsViewModel::SubtitleSize() const noexcept { return m_subtitleSize; }
    void SettingsViewModel::SubtitleSize(double value)
    {
        auto const next = std::clamp(value, 50.0, 200.0);
        if (std::abs(next - m_subtitleSize) < 0.01) return;
        m_subtitleSize = next;
        Raise(L"SubtitleSize");
        Raise(L"SubtitleSizeLabel");
        Raise(L"PreviewFontSize");
    }
    winrt::hstring SettingsViewModel::SubtitleSizeLabel() const
    {
        std::wostringstream label;
        label << static_cast<std::int32_t>(std::lround(m_subtitleSize)) << L"%";
        return winrt::hstring(label.str());
    }
    winrt::hstring SettingsViewModel::PreviewText() const { return ::HaloDesktop::Services::SampleData::Copy::PlayerSubtitle; }
    double SettingsViewModel::PreviewFontSize() const noexcept { return 15.0 * m_subtitleSize / 100.0; }
    Microsoft::UI::Xaml::Media::FontFamily SettingsViewModel::PreviewFontFamily() const
    {
        if (m_fontIndex == 1) return Microsoft::UI::Xaml::Media::FontFamily{ L"Georgia" };
        if (m_fontIndex == 2) return Microsoft::UI::Xaml::Media::FontFamily{ L"ms-appx:///Assets/Fonts/JetBrainsMono-Regular.ttf#JetBrains Mono" };
        return Microsoft::UI::Xaml::Media::FontFamily{ L"Segoe UI Variable Text" };
    }
    std::int32_t SettingsViewModel::FontIndex() const noexcept { return m_fontIndex; }
    std::int32_t SettingsViewModel::OutlineIndex() const noexcept { return m_outlineIndex; }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::ThinOutlineVisibility() const noexcept { return m_outlineIndex == 1 ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::HeavyOutlineVisibility() const noexcept { return m_outlineIndex == 2 ? Visible : Collapsed; }
    bool SettingsViewModel::AutoplayNext() const noexcept { return m_autoplayNext; }
    void SettingsViewModel::AutoplayNext(bool value) { if (m_autoplayNext != value) { m_autoplayNext = value; Raise(L"AutoplayNext"); } }
    bool SettingsViewModel::ResumePlayback() const noexcept { return m_resumePlayback; }
    void SettingsViewModel::ResumePlayback(bool value) { if (m_resumePlayback != value) { m_resumePlayback = value; Raise(L"ResumePlayback"); } }
    bool SettingsViewModel::HardwareDecoding() const noexcept { return m_hardwareDecoding; }
    void SettingsViewModel::HardwareDecoding(bool value) { if (m_hardwareDecoding != value) { m_hardwareDecoding = value; Raise(L"HardwareDecoding"); } }
    void SettingsViewModel::Refresh()
    {
        m_serverUrl = m_session->ServerUrl();
        m_userName = m_session->UserName();
        Raise(L"ServerUrl");
        Raise(L"UserName");
        Raise(L"SignedInLine");
    }
    void SettingsViewModel::AddAddon(winrt::hstring const& url)
    {
        auto valid = false;
        try
        {
            auto const uri = winrt::Windows::Foundation::Uri{ url };
            valid = !uri.Host().empty() && (uri.SchemeName() == L"https" || uri.SchemeName() == L"http");
        }
        catch (...)
        {
        }
        m_addonNoticeVisible = true;
        m_addonNoticeText = valid
            ? ::HaloDesktop::Services::SampleData::Copy::AddonInstallNotice
            : winrt::hstring{ L"Enter a valid addon URL." };
        Raise(L"AddonNoticeText");
        Raise(L"AddonNoticeVisibility");
    }
    void SettingsViewModel::ToggleAddon(winrt::hstring const& name, bool enabled)
    {
        m_addonService->Toggle(name, enabled);
    }
    void SettingsViewModel::RemoveAddon(winrt::hstring const& name)
    {
        m_addonService->Remove(name);
        SynchronizeAddons();
    }
    bool SettingsViewModel::IsLightTheme() const noexcept
    {
        return m_theme->Preference() == ::HaloDesktop::Services::ThemePreference::Light;
    }
    bool SettingsViewModel::IsDarkTheme() const noexcept
    {
        return m_theme->Preference() == ::HaloDesktop::Services::ThemePreference::Dark;
    }
    bool SettingsViewModel::IsSystemTheme() const noexcept
    {
        return m_theme->Preference() == ::HaloDesktop::Services::ThemePreference::System;
    }
    void SettingsViewModel::SetTheme(std::int32_t index)
    {
        using ::HaloDesktop::Services::ThemePreference;
        if (index < 0 || index > 2) return;
        m_theme->SetPreference(static_cast<ThemePreference>(index));
        Raise(L"IsLightTheme");
        Raise(L"IsDarkTheme");
        Raise(L"IsSystemTheme");
    }
    void SettingsViewModel::SetFont(std::int32_t index)
    {
        if (index < 0 || index > 2 || index == m_fontIndex) return;
        m_fontIndex = index;
        Raise(L"FontIndex");
        Raise(L"PreviewFontFamily");
    }
    void SettingsViewModel::SetOutline(std::int32_t index)
    {
        if (index < 0 || index > 2 || index == m_outlineIndex) return;
        m_outlineIndex = index;
        Raise(L"OutlineIndex");
        Raise(L"ThinOutlineVisibility");
        Raise(L"HeavyOutlineVisibility");
    }
    void SettingsViewModel::SignOut()
    {
        m_session->SignOut();
        Refresh();
        m_navigation->ShowOverlay(::HaloDesktop::Services::Page::Login);
    }
    void SettingsViewModel::SwitchServer()
    {
        m_session->ClearServer();
        Refresh();
        m_navigation->ShowOverlay(::HaloDesktop::Services::Page::Connect);
    }
    winrt::event_token SettingsViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void SettingsViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    void SettingsViewModel::SynchronizeAddons()
    {
        m_addons.Clear();
        for (auto const& addon : m_addonService->Items()) m_addons.Append(winrt::make<AddonRowViewModel>(addon));
    }
    void SettingsViewModel::Raise(wchar_t const* propertyName) { ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName); }
}
