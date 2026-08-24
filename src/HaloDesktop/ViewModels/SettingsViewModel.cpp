#include "pch.h"
#include "ViewModels/SettingsViewModel.h"
#if __has_include("AddonRowViewModel.g.cpp")
#include "AddonRowViewModel.g.cpp"
#endif
#if __has_include("SettingsViewModel.g.cpp")
#include "SettingsViewModel.g.cpp"
#endif

#include "Services/SampleData.h"
#include "Services/SettingsSyncService.h"
#include "ViewModels/ObservableHelper.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
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
    winrt::hstring AddonRowViewModel::Id() const { return m_addon.Id(); }
    winrt::hstring AddonRowViewModel::Initials() const { return m_addon.Initials(); }
    winrt::hstring AddonRowViewModel::Name() const { return m_addon.Name(); }
    winrt::hstring AddonRowViewModel::Version() const { return m_addon.Version(); }
    winrt::hstring AddonRowViewModel::Scope() const { return m_addon.Scope(); }
    winrt::hstring AddonRowViewModel::Provides() const { return m_addon.Provides(); }
    bool AddonRowViewModel::Enabled() const noexcept { return m_addon.Enabled(); }
    bool AddonRowViewModel::CanEdit() const noexcept { return m_addon.CanEdit(); }
    bool AddonRowViewModel::IsGlobal() const noexcept { return m_addon.IsGlobal(); }
    Microsoft::UI::Xaml::Visibility AddonRowViewModel::GlobalVisibility() const noexcept { return m_addon.Scope() == L"GLOBAL" ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility AddonRowViewModel::YoursVisibility() const noexcept { return m_addon.Scope() == L"YOURS" ? Visible : Collapsed; }

    SettingsViewModel::SettingsViewModel(::HaloDesktop::Services::AppServices const& services)
        : m_session(services.Session),
          m_theme(services.Theme),
          m_addonService(services.Addons),
          m_settings(services.SettingsSync),
          m_addons(winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>())
    {
        Refresh();
    }
    winrt::hstring SettingsViewModel::ServerUrl() const { return m_serverUrl; }
    winrt::hstring SettingsViewModel::UserName() const { return m_userName; }
    winrt::hstring SettingsViewModel::DisplayName() const
    {
        std::wstring displayName{ m_userName };
        if (!displayName.empty())
        {
            displayName.front() = static_cast<wchar_t>(std::towupper(displayName.front()));
        }
        return winrt::hstring{ displayName };
    }
    winrt::hstring SettingsViewModel::SignedInLine() const { return winrt::hstring(std::wstring(m_userName) + L" · premium"); }
    winrt::Windows::Foundation::IInspectable SettingsViewModel::Addons() const { return m_addons; }
    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> SettingsViewModel::AddonsView() const { return m_addons; }
    winrt::hstring SettingsViewModel::AddonNoticeText() const { return m_addonNoticeText; }
    bool SettingsViewModel::CanEditAddons() const noexcept { return m_addonService->CanEditLists(); }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::AddonNoticeVisibility() const noexcept { return m_addonNoticeVisible ? Visible : Collapsed; }
    double SettingsViewModel::SubtitleSize() const noexcept { return m_subtitleSize; }
    void SettingsViewModel::SubtitleSize(double value)
    {
        auto const next = std::clamp(value, 50.0, 200.0);
        if (std::abs(next - m_subtitleSize) < 0.01) return;
        m_subtitleSize = next;
        m_settings->SubtitleScalePercent(static_cast<std::int32_t>(std::lround(next)));
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
    bool SettingsViewModel::IsSystemFont() const noexcept { return m_fontIndex == 0; }
    bool SettingsViewModel::IsSerifFont() const noexcept { return m_fontIndex == 1; }
    bool SettingsViewModel::IsMonoFont() const noexcept { return m_fontIndex == 2; }
    bool SettingsViewModel::IsNoOutline() const noexcept { return m_outlineIndex == 0; }
    bool SettingsViewModel::IsThinOutline() const noexcept { return m_outlineIndex == 1; }
    bool SettingsViewModel::IsNormalOutline() const noexcept { return m_outlineIndex == 2; }
    bool SettingsViewModel::IsThickOutline() const noexcept { return m_outlineIndex == 3; }
    std::int32_t SettingsViewModel::AudioLanguageIndex() const noexcept { return m_audioLanguageIndex; }
    void SettingsViewModel::AudioLanguageIndex(std::int32_t value)
    {
        if (value < 0 || value > 4 || value == m_audioLanguageIndex) return;
        m_audioLanguageIndex = value;
        static constexpr wchar_t const* values[] = { L"eng", L"jpn", L"ger", L"spa", nullptr };
        m_settings->PreferredAudioLanguage(values[value] ? std::optional<winrt::hstring>{ values[value] } : std::nullopt);
        Raise(L"AudioLanguageIndex");
    }
    std::int32_t SettingsViewModel::SubtitleLanguageIndex() const noexcept { return m_subtitleLanguageIndex; }
    void SettingsViewModel::SubtitleLanguageIndex(std::int32_t value)
    {
        if (value < 0 || value > 4 || value == m_subtitleLanguageIndex) return;
        m_subtitleLanguageIndex = value;
        static constexpr wchar_t const* values[] = { nullptr, L"eng", L"jpn", L"ger", L"spa" };
        m_settings->PreferredSubtitleLanguage(values[value] ? std::optional<winrt::hstring>{ values[value] } : std::nullopt);
        Raise(L"SubtitleLanguageIndex");
    }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::ThinOutlineVisibility() const noexcept { return m_outlineIndex == 1 ? Visible : Collapsed; }
    Microsoft::UI::Xaml::Visibility SettingsViewModel::HeavyOutlineVisibility() const noexcept { return m_outlineIndex >= 2 ? Visible : Collapsed; }
    bool SettingsViewModel::AutoplayNext() const noexcept { return m_autoplayNext; }
    void SettingsViewModel::AutoplayNext(bool value) { if (m_autoplayNext != value) { m_autoplayNext = value; m_settings->AutoplayNextEpisode(value); Raise(L"AutoplayNext"); } }
    bool SettingsViewModel::SubtitleShadow() const noexcept { return m_subtitleShadow; }
    void SettingsViewModel::SubtitleShadow(bool value) { if (m_subtitleShadow != value) { m_subtitleShadow = value; m_settings->SubtitleShadow(value); Raise(L"SubtitleShadow"); } }
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
        Raise(L"DisplayName");
        Raise(L"SignedInLine");
        static_cast<void>(LoadAsync());
    }
    void SettingsViewModel::AddAddon(winrt::hstring const& url)
    {
        static_cast<void>(AddAddonAsync(url));
    }
    void SettingsViewModel::ToggleAddon(winrt::hstring const& id, bool enabled)
    {
        static_cast<void>(ToggleAddonAsync(id, enabled));
    }
    void SettingsViewModel::RemoveAddon(winrt::hstring const& id)
    {
        static_cast<void>(RemoveAddonAsync(id));
    }
    bool SettingsViewModel::IsGlobalAddon(winrt::hstring const& id) const
    {
        for (auto const& record : m_addonService->Records())
        {
            if (record.Id == id)
            {
                return record.IsGlobal;
            }
        }
        return false;
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
        static constexpr wchar_t const* families[] = { L"Segoe UI", L"Georgia", L"JetBrains Mono" };
        m_settings->SubtitleFontFamily(families[index]);
        Raise(L"FontIndex");
        Raise(L"IsSystemFont");
        Raise(L"IsSerifFont");
        Raise(L"IsMonoFont");
        Raise(L"PreviewFontFamily");
    }
    void SettingsViewModel::SetOutline(std::int32_t index)
    {
        if (index < 0 || index > 3 || index == m_outlineIndex) return;
        m_outlineIndex = index;
        static constexpr wchar_t const* outlines[] = { L"none", L"thin", L"normal", L"thick" };
        m_settings->SubtitleOutline(outlines[index]);
        Raise(L"OutlineIndex");
        Raise(L"IsNoOutline");
        Raise(L"IsThinOutline");
        Raise(L"IsNormalOutline");
        Raise(L"IsThickOutline");
        Raise(L"ThinOutlineVisibility");
        Raise(L"HeavyOutlineVisibility");
    }
    void SettingsViewModel::SignOut()
    {
        static_cast<void>(RunSignOutAsync());
    }
    winrt::Windows::Foundation::IAsyncAction SettingsViewModel::RunSignOutAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        co_await m_session->SignOutAsync();
        co_await uiContext;
        m_userName.clear();
        Raise(L"UserName");
        Raise(L"DisplayName");
        Raise(L"SignedInLine");
    }
    winrt::event_token SettingsViewModel::PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler) { return m_propertyChanged.add(handler); }
    void SettingsViewModel::PropertyChanged(winrt::event_token const& token) noexcept { m_propertyChanged.remove(token); }
    void SettingsViewModel::SynchronizeAddons()
    {
        m_addons.Clear();
        for (auto const& addon : m_addonService->Items()) m_addons.Append(winrt::make<AddonRowViewModel>(addon));
    }

    winrt::Windows::Foundation::IAsyncAction SettingsViewModel::LoadAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        bool failed{};
        try
        {
            co_await m_settings->LoadAsync();
            co_await m_addonService->LoadAsync();
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;
        if (failed)
        {
            m_addonNoticeVisible = true;
            m_addonNoticeText = L"Settings could not be refreshed. Your last saved preferences remain available.";
            Raise(L"AddonNoticeText");
            Raise(L"AddonNoticeVisibility");
            co_return;
        }

        m_subtitleSize = m_settings->SubtitleScalePercent();
            auto const family = m_settings->SubtitleFontFamily();
            m_fontIndex = family == L"Georgia" ? 1 : family == L"JetBrains Mono" ? 2 : 0;
            auto const outline = m_settings->SubtitleOutline();
            m_outlineIndex = outline == L"none" ? 0 : outline == L"thin" ? 1 : outline == L"thick" ? 3 : 2;
            m_subtitleShadow = m_settings->SubtitleShadow();
            m_autoplayNext = m_settings->AutoplayNextEpisode();
            auto const audio = m_settings->PreferredAudioLanguage();
            m_audioLanguageIndex = !audio ? 4 : *audio == L"jpn" ? 1 : *audio == L"ger" ? 2 : *audio == L"spa" ? 3 : 0;
            auto const subtitles = m_settings->PreferredSubtitleLanguage();
            m_subtitleLanguageIndex = !subtitles ? 0 : *subtitles == L"jpn" ? 2 : *subtitles == L"ger" ? 3 : *subtitles == L"spa" ? 4 : 1;
            SynchronizeAddons();
            Raise(L"CanEditAddons");
            Raise(L"SubtitleSize");
            Raise(L"SubtitleSizeLabel");
            Raise(L"PreviewFontFamily");
            Raise(L"PreviewFontSize");
            Raise(L"FontIndex");
            Raise(L"OutlineIndex");
            Raise(L"IsSystemFont");
            Raise(L"IsSerifFont");
            Raise(L"IsMonoFont");
            Raise(L"IsNoOutline");
            Raise(L"IsThinOutline");
            Raise(L"IsNormalOutline");
            Raise(L"IsThickOutline");
            Raise(L"ThinOutlineVisibility");
            Raise(L"HeavyOutlineVisibility");
            Raise(L"SubtitleShadow");
            Raise(L"AutoplayNext");
            Raise(L"AudioLanguageIndex");
            Raise(L"SubtitleLanguageIndex");
        if (!m_addonService->CanEditLists())
        {
            m_addonNoticeVisible = true;
            m_addonNoticeText = L"This addon list cannot be edited safely because one or more transport URLs are hidden.";
            Raise(L"AddonNoticeText");
            Raise(L"AddonNoticeVisibility");
        }
    }

    winrt::Windows::Foundation::IAsyncAction SettingsViewModel::AddAddonAsync(winrt::hstring url)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        bool failed{};
        try
        {
            co_await m_addonService->AddAsync(std::move(url));
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;
        SynchronizeAddons();
        m_addonNoticeVisible = true;
        m_addonNoticeText = failed
            ? winrt::hstring{ L"The addon could not be installed. Check the URL and try again." }
            : winrt::hstring{ L"Addon installed." };
        Raise(L"AddonNoticeText");
        Raise(L"AddonNoticeVisibility");
    }

    winrt::Windows::Foundation::IAsyncAction SettingsViewModel::ToggleAddonAsync(winrt::hstring id, bool enabled)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        bool failed{};
        try
        {
            co_await m_addonService->SetCatalogsVisibleAsync(std::move(id), enabled);
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;
        SynchronizeAddons();
        if (failed)
        {
            m_addonNoticeVisible = true;
            m_addonNoticeText = L"The addon visibility could not be changed.";
            Raise(L"AddonNoticeText");
            Raise(L"AddonNoticeVisibility");
        }
    }

    winrt::Windows::Foundation::IAsyncAction SettingsViewModel::RemoveAddonAsync(winrt::hstring id)
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        bool failed{};
        try
        {
            co_await m_addonService->RemoveAsync(std::move(id));
        }
        catch (...)
        {
            failed = true;
        }
        co_await uiContext;
        SynchronizeAddons();
        if (failed)
        {
            m_addonNoticeVisible = true;
            m_addonNoticeText = L"The addon could not be removed.";
            Raise(L"AddonNoticeText");
            Raise(L"AddonNoticeVisibility");
        }
    }
    void SettingsViewModel::Raise(wchar_t const* propertyName) { ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName); }
}
