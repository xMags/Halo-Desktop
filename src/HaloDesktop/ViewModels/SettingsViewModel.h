#pragma once

#include "AddonRowViewModel.g.h"
#include "SettingsViewModel.g.h"
#include "Services/AppServices.h"
#include "Services/ServiceInterfaces.h"
#include "Services/ThemeService.h"

#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    struct AddonRowViewModel : AddonRowViewModelT<AddonRowViewModel>
    {
        explicit AddonRowViewModel(winrt::HaloDesktop::Addon addon);
        [[nodiscard]] winrt::hstring Id() const;
        [[nodiscard]] winrt::hstring Initials() const;
        [[nodiscard]] winrt::hstring Name() const;
        [[nodiscard]] winrt::hstring Version() const;
        [[nodiscard]] winrt::hstring Scope() const;
        [[nodiscard]] winrt::hstring Provides() const;
        [[nodiscard]] bool Enabled() const noexcept;
        [[nodiscard]] bool CanEdit() const noexcept;
        [[nodiscard]] bool IsGlobal() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility GlobalVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility YoursVisibility() const noexcept;

    private:
        winrt::HaloDesktop::Addon m_addon{ nullptr };
    };

    struct SettingsViewModel : SettingsViewModelT<SettingsViewModel>
    {
        explicit SettingsViewModel(::HaloDesktop::Services::AppServices const& services);
        [[nodiscard]] winrt::hstring ServerUrl() const;
        [[nodiscard]] winrt::hstring UserName() const;
        [[nodiscard]] winrt::hstring DisplayName() const;
        [[nodiscard]] winrt::hstring SignedInLine() const;
        [[nodiscard]] winrt::hstring AccountRoleLine() const;
        [[nodiscard]] winrt::hstring ServerStatusLine() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ServerCheckingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ServerConnectedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ServerUnavailableVisibility() const noexcept;
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Addons() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> AddonsView() const;
        [[nodiscard]] winrt::hstring AddonNoticeText() const;
        [[nodiscard]] bool CanEditAddons() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AddonNoticeVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AddonLoadingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AddonErrorVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AddonEmptyVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AddonContentVisibility() const noexcept;
        [[nodiscard]] double SubtitleSize() const noexcept;
        void SubtitleSize(double value);
        [[nodiscard]] winrt::hstring SubtitleSizeLabel() const;
        [[nodiscard]] winrt::hstring PreviewText() const;
        [[nodiscard]] double PreviewFontSize() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Media::FontFamily PreviewFontFamily() const;
        [[nodiscard]] std::int32_t FontIndex() const noexcept;
        [[nodiscard]] std::int32_t OutlineIndex() const noexcept;
        [[nodiscard]] bool IsSystemFont() const noexcept;
        [[nodiscard]] bool IsSerifFont() const noexcept;
        [[nodiscard]] bool IsMonoFont() const noexcept;
        [[nodiscard]] bool IsNoOutline() const noexcept;
        [[nodiscard]] bool IsThinOutline() const noexcept;
        [[nodiscard]] bool IsNormalOutline() const noexcept;
        [[nodiscard]] bool IsThickOutline() const noexcept;
        [[nodiscard]] std::int32_t AudioLanguageIndex() const noexcept;
        void AudioLanguageIndex(std::int32_t value);
        [[nodiscard]] std::int32_t SubtitleLanguageIndex() const noexcept;
        void SubtitleLanguageIndex(std::int32_t value);
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ThinOutlineVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility HeavyOutlineVisibility() const noexcept;
        [[nodiscard]] bool AutoplayNext() const noexcept;
        void AutoplayNext(bool value);
        [[nodiscard]] bool SubtitleShadow() const noexcept;
        void SubtitleShadow(bool value);
        [[nodiscard]] bool ResumePlayback() const noexcept;
        void ResumePlayback(bool value);
        [[nodiscard]] bool HardwareDecoding() const noexcept;
        void HardwareDecoding(bool value);
        void Refresh();
        void ProbeHealth();
        void CancelHealthProbe();
        void AddAddon(winrt::hstring const& url);
        void ToggleAddon(winrt::hstring const& name, bool enabled);
        void RemoveAddon(winrt::hstring const& name);
        [[nodiscard]] bool IsGlobalAddon(winrt::hstring const& id) const;
        [[nodiscard]] bool IsLightTheme() const noexcept;
        [[nodiscard]] bool IsDarkTheme() const noexcept;
        [[nodiscard]] bool IsSystemTheme() const noexcept;
        void SetTheme(std::int32_t index);
        void SetFont(std::int32_t index);
        void SetOutline(std::int32_t index);
        void SignOut();
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        winrt::Windows::Foundation::IAsyncAction LoadAsync();
        winrt::Windows::Foundation::IAsyncAction ProbeHealthAsync(std::uint64_t version);
        winrt::Windows::Foundation::IAsyncAction AddAddonAsync(winrt::hstring url);
        winrt::Windows::Foundation::IAsyncAction ToggleAddonAsync(winrt::hstring id, bool enabled);
        winrt::Windows::Foundation::IAsyncAction RemoveAddonAsync(winrt::hstring id);
        winrt::Windows::Foundation::IAsyncAction RunSignOutAsync();
        void SynchronizeAddons();
        void RaiseAddonState();
        void Raise(wchar_t const* propertyName);

        enum class HealthState
        {
            Checking,
            Connected,
            Unavailable,
        };
        std::shared_ptr<::HaloDesktop::Services::ISessionService> m_session;
        std::shared_ptr<::HaloDesktop::Services::ThemeService> m_theme;
        std::shared_ptr<::HaloDesktop::Services::IAddonService> m_addonService;
        std::shared_ptr<::HaloDesktop::Services::SettingsSyncService> m_settings;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_addons{ nullptr };
        winrt::hstring m_serverUrl;
        winrt::hstring m_userName;
        winrt::hstring m_addonNoticeText;
        winrt::hstring m_serverStatusLine{ L"CHECKING…" };
        double m_subtitleSize{ 100.0 };
        std::int32_t m_fontIndex{};
        std::int32_t m_outlineIndex{ 1 };
        std::int32_t m_audioLanguageIndex{ 2 };
        std::int32_t m_subtitleLanguageIndex{};
        bool m_addonNoticeVisible{};
        bool m_addonsLoading{};
        bool m_addonsError{};
        bool m_autoplayNext{ true };
        bool m_subtitleShadow{ true };
        bool m_resumePlayback{ true };
        bool m_hardwareDecoding{};
        HealthState m_healthState{ HealthState::Checking };
        std::uint64_t m_loadVersion{};
        std::uint64_t m_healthRequestVersion{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
