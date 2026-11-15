#pragma once

#include "Api/Dto.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <pplawait.h>
#include <ppltasks.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.Data.Json.h>

namespace HaloDesktop::Api
{
    class ApiClient;
}

namespace HaloDesktop::Storage
{
    class AppStoragePaths;
}

namespace HaloDesktop::Services
{
    class QueryCache;

    // UI-thread-only raw-document settings repository. Unknown JSON keys are
    // retained across every typed mutation and mirrored locally for offline use.
    //
    // A save replaces the whole document, so nothing is saved or mirrored for
    // an account until a real document (the server's, or that account's
    // mirror) has been read. A change made before then is shown at once and
    // kept waiting while a read runs; it is applied on top of the document
    // when one arrives, and dropped when none can be had.
    class SettingsSyncService final
    {
    public:
        SettingsSyncService(
            std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
            std::shared_ptr<QueryCache> queryCache,
            std::shared_ptr<::HaloDesktop::Storage::AppStoragePaths const> paths,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);

        [[nodiscard]] concurrency::task<void> LoadAsync();
        [[nodiscard]] winrt::Windows::Data::Json::JsonObject RawValue() const;
        [[nodiscard]] std::int64_t UpdatedAt() const noexcept;

        [[nodiscard]] std::optional<winrt::hstring> PreferredAudioLanguage() const;
        void PreferredAudioLanguage(std::optional<winrt::hstring> value);
        [[nodiscard]] std::optional<winrt::hstring> PreferredSubtitleLanguage() const;
        void PreferredSubtitleLanguage(std::optional<winrt::hstring> value);
        [[nodiscard]] std::int32_t SubtitleScalePercent() const noexcept;
        void SubtitleScalePercent(std::int32_t value);
        [[nodiscard]] winrt::hstring SubtitleFontFamily() const;
        void SubtitleFontFamily(winrt::hstring value);
        [[nodiscard]] winrt::hstring SubtitleOutline() const;
        void SubtitleOutline(winrt::hstring value);
        [[nodiscard]] bool SubtitleShadow() const noexcept;
        void SubtitleShadow(bool value);
        // True leaves a styled track (ASS/SSA) rendering the way its author wrote it.
        // False makes the Halo font, size and border win over the track's own styling.
        // Plain text tracks carry no styling either way, so Halo's always applies there.
        [[nodiscard]] bool SubtitleTrackStyling() const noexcept;
        void SubtitleTrackStyling(bool value);
        [[nodiscard]] bool AutoplayNextEpisode() const noexcept;
        void AutoplayNextEpisode(bool value);
        void OnAccountChanged(winrt::hstring const& serverUrl, winrt::hstring const& userId);

    private:
        [[nodiscard]] concurrency::task<std::optional<::HaloDesktop::Api::Dto::SettingsPayload>> ReadMirrorAsync();
        [[nodiscard]] concurrency::task<void> WriteMirrorAsync(::HaloDesktop::Api::Dto::SettingsPayload payload);
        [[nodiscard]] concurrency::task<void> SaveAsync(
            winrt::Windows::Data::Json::JsonObject snapshot,
            std::int64_t updatedAt);
        void Apply(::HaloDesktop::Api::Dto::SettingsPayload payload);
        void SetString(wchar_t const* key, std::optional<winrt::hstring> value, bool debounce);
        void SetBoolean(wchar_t const* key, bool value);
        // A null value removes the key.
        void ApplyEdit(wchar_t const* key, winrt::Windows::Data::Json::IJsonValue const& value, bool debounce);
        void ReplayUnreadEdits();
        void Touch(bool debounce);
        [[nodiscard]] std::int64_t NextTimestamp() const noexcept;
        [[nodiscard]] winrt::Windows::Data::Json::JsonObject Snapshot() const;

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::shared_ptr<QueryCache> m_queryCache;
        std::filesystem::path m_mirrorRoot;
        std::filesystem::path m_mirrorPath;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_debounceTimer{ nullptr };
        winrt::Windows::Data::Json::JsonObject m_value;
        std::int64_t m_updatedAt{};
        std::uint64_t m_writeVersion{};
        std::mutex m_mirrorMutex;
        std::uint64_t m_accountVersion{};

        struct UnreadEdit final
        {
            std::wstring Key;
            // Null when the edit removes the key.
            winrt::Windows::Data::Json::IJsonValue Value{ nullptr };
        };

        // Whether this account's document has been read yet.
        bool m_documentRead{};
        // Edits made before that, newest value per key, in first-edit order.
        std::vector<UnreadEdit> m_unreadEdits;
    };
}
