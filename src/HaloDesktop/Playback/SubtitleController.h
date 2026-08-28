#pragma once

#include "Playback/IPlaybackEngine.h"
#include "Playback/PlaybackPolicy.h"
#include "Playback/TemporaryFileCollection.h"

#include <functional>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <ppltasks.h>
#include <winrt/HaloDesktop.h>

namespace HaloDesktop::Api { class ApiClient; }
namespace HaloDesktop::Services { class DevicePreferencesStore; class IDownloadService; class SettingsSyncService; }
namespace HaloDesktop::Storage { class AppStoragePaths; }

namespace HaloDesktop::Playback
{
    // HashMatched separates "synced to this exact file" from "matched by name",
    // which is the only quality signal an addon subtitle carries before download.
    struct AddonSubtitleDisplay final{winrt::hstring Key,Language,Addon,Variant;bool HashMatched{};};

    // UI-thread-only subtitle pipeline. Provider URLs remain in the native map;
    // observable consumers receive opaque keys and display labels only.
    class SubtitleController final : public std::enable_shared_from_this<SubtitleController>
    {
    public:
        SubtitleController(
            std::shared_ptr<Api::ApiClient> api,
            std::shared_ptr<IPlaybackEngine> engine,
            std::shared_ptr<Services::SettingsSyncService> settings,
            std::shared_ptr<Services::IDownloadService> downloads,
            std::shared_ptr<Services::DevicePreferencesStore> preferences,
            std::shared_ptr<Storage::AppStoragePaths const> paths);
        ~SubtitleController();
        [[nodiscard]] concurrency::task<void> PrepareAsync(winrt::HaloDesktop::PlaybackRequest request);
        [[nodiscard]] concurrency::task<void> SelectAsync(winrt::hstring key,bool deliberate=true);
        void SelectTrack(std::int64_t id);
        void Disable();
        [[nodiscard]] std::vector<AddonSubtitleDisplay> Choices()const;
        // Empty unless the live subtitle track came from one of the choices above.
        [[nodiscard]] winrt::hstring SelectedChoiceKey()const;
        void SetChoicesChangedHandler(std::function<void()> handler);
        void SetErrorHandler(std::function<void()> handler);
        void RefreshPreferences();
        // Appearance only. RefreshPreferences also re-runs track selection, which must
        // not happen while a live control such as the size slider is being dragged.
        void RefreshStyle();
        void Stop()noexcept;
        void CleanupTemporaryFiles()noexcept;

    private:
        struct NativeChoice final{AddonSubtitleDisplay Display;winrt::hstring AddonId,SubtitleId,Url,Lang;};
        struct SelectionMemory final
        {
            SubtitleIntentKind Intent{SubtitleIntentKind::Automatic};
            std::optional<winrt::hstring>Identity;
            std::optional<winrt::hstring>Fingerprint;
            std::optional<winrt::hstring>Language;
            bool ExactVideo{};
        };
        void ApplyStyle();
        void SweepExternalTracks();
        void OnEngineChanged();
        void TryApplySelection();
        void RememberAddon(NativeChoice const& choice);
        void RememberTrack(TrackInfo const& track);
        void RememberOff();
        void StoreIntent(
            SubtitleIntentKind intent,
            std::optional<winrt::hstring> identity,
            std::optional<winrt::hstring> fingerprint,
            std::optional<winrt::hstring> language,
            bool includeItem);
        [[nodiscard]] SelectionMemory ReadSelectionMemory()const;
        [[nodiscard]] std::optional<winrt::hstring> ChoiceByIdentity(winrt::hstring const&identity)const;
        [[nodiscard]] std::optional<winrt::hstring> ChoiceByLanguage(winrt::hstring const&language)const;
        [[nodiscard]] concurrency::task<std::wstring> DownloadAsync(NativeChoice const& choice);
        void NotifyError()noexcept;

        std::shared_ptr<Api::ApiClient>m_api;std::shared_ptr<IPlaybackEngine>m_engine;std::shared_ptr<Services::SettingsSyncService>m_settings;std::shared_ptr<Services::IDownloadService>m_downloads;std::shared_ptr<Services::DevicePreferencesStore>m_preferences;std::shared_ptr<Storage::AppStoragePaths const>m_paths;
        winrt::HaloDesktop::PlaybackRequest m_request{nullptr};std::unordered_map<std::wstring,NativeChoice>m_choices;std::vector<AddonSubtitleDisplay>m_display;
        std::optional<std::filesystem::path> m_localSubtitlePath;
        TemporaryFileCollection m_temporaryFiles;
        std::function<void()>m_changed,m_error;PlaybackChangedToken m_engineToken{};std::uint64_t m_fileSerial{},m_appliedSerial{},m_generation{},m_selectionAttempt{},m_pendingSelectionAttempt{},m_preferenceRevision{},m_appliedPreferenceRevision{},m_initialSubtitleSelectionSerial{},m_autoSubtitleSelectionSerial{};bool m_discoveryComplete{},m_pendingSelectionDeliberate{},m_handlingEngineChange{};
    };

    [[nodiscard]] winrt::hstring SubtitleLanguageLabel(winrt::hstring code);
}
