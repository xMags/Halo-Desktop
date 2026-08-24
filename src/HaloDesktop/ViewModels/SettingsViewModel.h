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
        [[nodiscard]] winrt::hstring Initials() const;
        [[nodiscard]] winrt::hstring Name() const;
        [[nodiscard]] winrt::hstring Version() const;
        [[nodiscard]] winrt::hstring Scope() const;
        [[nodiscard]] winrt::hstring Provides() const;
        [[nodiscard]] bool Enabled() const noexcept;
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
        [[nodiscard]] winrt::Windows::Foundation::IInspectable Addons() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> AddonsView() const;
        [[nodiscard]] winrt::hstring AddonNoticeText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AddonNoticeVisibility() const noexcept;
        [[nodiscard]] double SubtitleSize() const noexcept;
        void SubtitleSize(double value);
        [[nodiscard]] winrt::hstring SubtitleSizeLabel() const;
        [[nodiscard]] winrt::hstring PreviewText() const;
        [[nodiscard]] double PreviewFontSize() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Media::FontFamily PreviewFontFamily() const;
        [[nodiscard]] std::int32_t FontIndex() const noexcept;
        [[nodiscard]] std::int32_t OutlineIndex() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ThinOutlineVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility HeavyOutlineVisibility() const noexcept;
        [[nodiscard]] bool AutoplayNext() const noexcept;
        void AutoplayNext(bool value);
        [[nodiscard]] bool ResumePlayback() const noexcept;
        void ResumePlayback(bool value);
        [[nodiscard]] bool HardwareDecoding() const noexcept;
        void HardwareDecoding(bool value);
        void Refresh();
        void AddAddon(winrt::hstring const& url);
        void ToggleAddon(winrt::hstring const& name, bool enabled);
        void RemoveAddon(winrt::hstring const& name);
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
        void SynchronizeAddons();
        void Raise(wchar_t const* propertyName);
        std::shared_ptr<::HaloDesktop::Services::ISessionService> m_session;
        std::shared_ptr<::HaloDesktop::Services::ThemeService> m_theme;
        std::shared_ptr<::HaloDesktop::Services::IAddonService> m_addonService;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_addons{ nullptr };
        winrt::hstring m_serverUrl;
        winrt::hstring m_userName;
        winrt::hstring m_addonNoticeText;
        double m_subtitleSize{ 100.0 };
        std::int32_t m_fontIndex{};
        std::int32_t m_outlineIndex{ 1 };
        bool m_addonNoticeVisible{};
        bool m_autoplayNext{ true };
        bool m_resumePlayback{ true };
        bool m_hardwareDecoding{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
