#pragma once

#include "AddonRowViewModel.g.h"
#include "SettingsViewModel.g.h"
#include "Services/AddonRowPolicy.h"
#include "Services/AppServices.h"
#include "Services/BuildInfoReader.h"
#include "Services/ServiceInterfaces.h"
#include "Services/ThemeService.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Data.h>

namespace winrt::HaloDesktop::implementation
{
    struct AddonRowViewModel : AddonRowViewModelT<AddonRowViewModel>
    {
        struct Content final
        {
            winrt::hstring Id;
            winrt::hstring Name;
            winrt::hstring Version;
            winrt::hstring Provides;
            bool IsGlobal{};
            bool CatalogsShown{};
            ::HaloDesktop::Services::AddonRowActions Actions;
        };

        explicit AddonRowViewModel(Content content);
        [[nodiscard]] winrt::hstring Id() const;
        [[nodiscard]] winrt::hstring Initials() const;
        [[nodiscard]] winrt::hstring Name() const;
        [[nodiscard]] winrt::hstring Version() const;
        [[nodiscard]] winrt::hstring Provides() const;
        [[nodiscard]] bool IsGlobal() const noexcept;
        [[nodiscard]] bool IsShadowed() const noexcept;
        [[nodiscard]] bool CatalogsShown() const noexcept;
        // Written by the row's switch. The row keeps showing what the server
        // holds: a different value is a request, passed to the handler, and
        // the rows are rebuilt once it is answered. Writing the value the row
        // already has does nothing, which is what a switch does whenever a
        // list row is built or reused for this addon.
        void CatalogsShown(bool value);
        [[nodiscard]] bool CatalogSwitchEnabled() const noexcept;
        [[nodiscard]] winrt::hstring CatalogSwitchLabel() const;
        [[nodiscard]] winrt::hstring GripToolTip() const;
        [[nodiscard]] double IdentityOpacity() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility GlobalVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility YoursVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CatalogSwitchVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility InstallForEveryoneVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility RemoveVisibility() const noexcept;
        [[nodiscard]] bool ListActionsEnabled() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Thickness DividerThickness() const noexcept;
        // Room kept at the row's end for buttons: the widest any row in the card
        // needs, so the switches line up, and none when no row has a button.
        [[nodiscard]] double ActionSlotWidth() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ActionSlotVisibility() const noexcept;
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

        [[nodiscard]] bool IsMovable() const noexcept;
        void SetListBusy(bool busy);
        void SetFirstInCard(bool first);
        void SetActionSlotWidth(double width);
        // What this row's own buttons need; the card reserves the widest.
        [[nodiscard]] double ActionsWidth() const noexcept;
        // Called on the UI thread with the addon's id and the value asked for.
        void SetCatalogToggleHandler(std::function<void(winrt::hstring const&, bool)> handler);

    private:
        void Raise(wchar_t const* propertyName);

        Content m_content;
        std::function<void(winrt::hstring const&, bool)> m_catalogToggle;
        winrt::hstring m_initials;
        bool m_firstInCard{};
        double m_actionSlotWidth{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };

    struct SettingsViewModel : SettingsViewModelT<SettingsViewModel>
    {
        explicit SettingsViewModel(::HaloDesktop::Services::AppServices const& services);
        ~SettingsViewModel();
        [[nodiscard]] winrt::hstring UserName() const;
        [[nodiscard]] winrt::hstring DisplayName() const;
        [[nodiscard]] winrt::hstring SignedInLine() const;
        [[nodiscard]] winrt::hstring AccountRoleLine() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SignedInAccountVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility DeviceAccountVisibility() const noexcept;
        [[nodiscard]] winrt::hstring VersionLine() const;
        [[nodiscard]] winrt::hstring ServerStatusLine() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ServerCheckingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ServerConnectedVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ServerUnavailableVisibility() const noexcept;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> GlobalAddons() const;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> OwnAddons() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility GlobalAddonsVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility OwnAddonsVisibility() const noexcept;
        [[nodiscard]] bool CanReorderGlobal() const noexcept;
        [[nodiscard]] bool CanReorderOwn() const noexcept;
        [[nodiscard]] winrt::hstring AddonUrl() const;
        void AddonUrl(winrt::hstring const& value);
        [[nodiscard]] bool CanAddAddon() const;
        [[nodiscard]] winrt::hstring AddButtonText() const;
        [[nodiscard]] winrt::hstring AddErrorText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AddErrorVisibility() const noexcept;
        [[nodiscard]] winrt::hstring ListErrorText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ListErrorVisibility() const noexcept;
        [[nodiscard]] winrt::hstring AddonNoticeText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AddonNoticeVisibility() const noexcept;
        [[nodiscard]] winrt::hstring AccountNoticeText() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AccountNoticeVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AddonLoadingVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AddonErrorVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AddonEmptyVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility AddonContentVisibility() const noexcept;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> CatalogLanguageOptions() const;
        [[nodiscard]] std::int32_t CatalogLanguageIndex() const noexcept;
        void CatalogLanguageIndex(std::int32_t value);
        [[nodiscard]] Microsoft::UI::Xaml::Visibility CatalogLanguageVisibility() const noexcept;
        [[nodiscard]] double SubtitleSize() const noexcept;
        void SubtitleSize(double value);
        [[nodiscard]] winrt::hstring SubtitleSizeLabel() const;
        [[nodiscard]] winrt::hstring PreviewText() const;
        [[nodiscard]] double PreviewFontSize() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Media::FontFamily PreviewFontFamily() const;
        [[nodiscard]] std::int32_t FontIndex() const noexcept;
        [[nodiscard]] std::int32_t OutlineIndex() const noexcept;
        [[nodiscard]] bool IsDefaultFont() const noexcept;
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
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PreviewOutlineVisibility() const noexcept;
        [[nodiscard]] double PreviewOutlineOffset() const noexcept;
        [[nodiscard]] double PreviewOutlineNegativeOffset() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility PreviewShadowVisibility() const noexcept;
        [[nodiscard]] double PreviewShadowOffset() const noexcept;
        [[nodiscard]] bool AutoplayNext() const noexcept;
        void AutoplayNext(bool value);
        [[nodiscard]] bool SubtitleShadow() const noexcept;
        void SubtitleShadow(bool value);
        [[nodiscard]] bool SubtitleTrackStyling() const noexcept;
        void SubtitleTrackStyling(bool value);
        [[nodiscard]] bool ResumePlayback() const noexcept;
        void ResumePlayback(bool value);
        [[nodiscard]] bool HardwareDecoding() const noexcept;
        void HardwareDecoding(bool value);
        [[nodiscard]] bool DiscordPresence() const noexcept;
        void DiscordPresence(bool value);
        // The window's translucent (acrylic) ground on this PC; applied at once.
        [[nodiscard]] bool TranslucentWindow() const noexcept;
        void TranslucentWindow(bool value);
        // Windows can draw it here; the switch is unavailable otherwise.
        [[nodiscard]] bool TranslucentWindowSupported() const noexcept;
        void Refresh();
        void ProbeHealth();
        void CancelHealthProbe();
        void AddAddon();
        void ToggleAddon(winrt::hstring const& id, bool enabled);
        void RemoveAddon(winrt::hstring const& id);
        void InstallForEveryone(winrt::hstring const& id);
        void CommitReorder(bool global);
        void MoveAddon(winrt::hstring const& id, std::int32_t offset);
        [[nodiscard]] bool IsLightTheme() const noexcept;
        [[nodiscard]] bool IsDarkTheme() const noexcept;
        [[nodiscard]] bool IsSystemTheme() const noexcept;
        void SetTheme(std::int32_t index);
        void SetFont(std::int32_t index);
        void SetOutline(std::int32_t index);
        void SignOut();
        void SignInToSync();
        winrt::event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

        // Native-only calls from the page, which asks before it acts.
        // The confirmation for removing a row, or nothing when it cannot be removed now.
        [[nodiscard]] std::optional<::HaloDesktop::Services::ConfirmationCopy> RemoveConfirmation(winrt::hstring const& id) const;
        // The confirmation for sharing a row, or nothing when it cannot be
        // shared; a global list read without its URLs sets the error line.
        [[nodiscard]] std::optional<::HaloDesktop::Services::ConfirmationCopy> BeginInstallForEveryone(winrt::hstring const& id);
        struct RowMoves final
        {
            bool Offered{};
            bool CanMoveUp{};
            bool CanMoveDown{};
        };
        [[nodiscard]] RowMoves MovesFor(winrt::hstring const& id) const;
        // Whether a drag of these rows may start now.
        [[nodiscard]] bool CanDragRows(std::vector<winrt::hstring> const& ids) const;
        // The signed-in account's picture fingerprint, empty without one.
        [[nodiscard]] winrt::hstring AccountPictureVersion() const;
        [[nodiscard]] winrt::hstring AccountUserId() const;

    private:
        enum class HealthState
        {
            Checking,
            Connected,
            Unavailable,
        };

        winrt::Windows::Foundation::IAsyncAction LoadAsync();
        winrt::Windows::Foundation::IAsyncAction ProbeHealthAsync(std::uint64_t version);
        winrt::Windows::Foundation::IAsyncAction ReadBuildInfoAsync();
        winrt::Windows::Foundation::IAsyncAction AddAddonAsync(winrt::hstring url);
        winrt::Windows::Foundation::IAsyncAction ToggleAddonAsync(winrt::hstring id, winrt::hstring name, bool enabled);
        winrt::Windows::Foundation::IAsyncAction RemoveAddonAsync(winrt::hstring id, winrt::hstring name);
        winrt::Windows::Foundation::IAsyncAction InstallForEveryoneAsync(winrt::hstring id, winrt::hstring name);
        winrt::Windows::Foundation::IAsyncAction SaveOrderAsync(bool global, std::vector<winrt::hstring> ids);
        winrt::Windows::Foundation::IAsyncAction RunSignOutAsync();
        [[nodiscard]] std::optional<::HaloDesktop::Api::Dto::AddonRecord> FindRecord(winrt::hstring const& id) const;
        [[nodiscard]] bool ListShown() const noexcept;
        void SynchronizeAddons();
        void ClearAddonRows();
        void RefreshRowDividers();
        void RefreshActionSlots();
        void SetListBusy(bool busy);
        void SetAdding(bool adding);
        void SetAddError(std::wstring text);
        void SetListError(std::wstring text);
        void RebuildCatalogLanguage();
        void RaiseAddonState();
        void RaiseAccountIdentity();
        void RaisePreviewMetrics();
        void Raise(wchar_t const* propertyName);
        void ReadSyncedSettings();
        void SubscribeToServiceChanges();
        void OnProfileChanged();

        std::shared_ptr<::HaloDesktop::Services::ISessionService> m_session;
        std::shared_ptr<::HaloDesktop::Services::NavigationService> m_navigation;
        std::shared_ptr<::HaloDesktop::Services::ThemeService> m_theme;
        std::shared_ptr<::HaloDesktop::Shell::WindowBackdropService> m_windowBackdrop;
        std::shared_ptr<::HaloDesktop::Services::IAddonService> m_addonService;
        std::shared_ptr<::HaloDesktop::Services::ICatalogService> m_catalog;
        std::shared_ptr<::HaloDesktop::Services::SettingsSyncService> m_settings;
        std::shared_ptr<::HaloDesktop::Services::PlaybackPreferences> m_playbackPreferences;
        std::shared_ptr<::HaloDesktop::Services::DiscordPresenceService> m_discordPresenceService;
        std::shared_ptr<::HaloDesktop::Playback::IPlaybackEngine> m_playback;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_globalAddons{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_ownAddons{ nullptr };
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> m_catalogLanguageOptions{ nullptr };
        std::vector<std::wstring> m_catalogLanguageChoices;
        std::int32_t m_catalogLanguageIndex{};
        winrt::hstring m_userName;
        winrt::hstring m_addonUrl;
        std::wstring m_addError;
        std::wstring m_listError;
        winrt::hstring m_addonNoticeText;
        winrt::hstring m_accountNoticeText;
        winrt::hstring m_serverStatusLine{ L"CHECKING…" };
        std::optional<::HaloDesktop::Services::BuildInfo> m_buildInfo;
        double m_subtitleSize{ 100.0 };
        std::int32_t m_fontIndex{};
        std::int32_t m_outlineIndex{ 1 };
        std::int32_t m_audioLanguageIndex{ 2 };
        std::int32_t m_subtitleLanguageIndex{};
        bool m_addonNoticeVisible{};
        bool m_accountNoticeVisible{};
        bool m_addonsLoading{};
        bool m_addonsError{};
        // Rows for this account are on screen: a refresh keeps them, and only
        // a first load with nothing to show is an error.
        bool m_listShown{};
        // One whole-list save at a time; each sends the list as it was read.
        bool m_listBusy{};
        bool m_adding{};
        bool m_buildInfoRequested{};
        bool m_autoplayNext{ true };
        bool m_subtitleShadow{ true };
        bool m_subtitleTrackStyling{ true };
        bool m_resumePlayback{ true };
        bool m_hardwareDecoding{};
        bool m_discordPresence{ true };
        HealthState m_healthState{ HealthState::Checking };
        std::uint64_t m_loadVersion{};
        std::uint64_t m_healthRequestVersion{};
        std::uint64_t m_settingsRevertedToken{};
        ::HaloDesktop::Services::IdentityChangedToken m_profileChangedToken{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
