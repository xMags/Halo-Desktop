#pragma once

#include "Api/Dto.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.Data.Json.h>

namespace HaloDesktop::Api
{
    class ApiClient;
}

namespace HaloDesktop::Services
{
    class QueryCache;

    // UI-thread-only raw-document settings repository. Unknown JSON keys are
    // retained across every typed mutation and mirrored locally for offline use.
    class SettingsSyncService final
    {
    public:
        SettingsSyncService(
            std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
            std::shared_ptr<QueryCache> queryCache,
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
        [[nodiscard]] bool AutoplayNextEpisode() const noexcept;
        void AutoplayNextEpisode(bool value);

    private:
        [[nodiscard]] concurrency::task<std::optional<::HaloDesktop::Api::Dto::SettingsPayload>> ReadMirrorAsync();
        [[nodiscard]] concurrency::task<void> WriteMirrorAsync(::HaloDesktop::Api::Dto::SettingsPayload payload);
        [[nodiscard]] concurrency::task<void> SaveAsync(
            winrt::Windows::Data::Json::JsonObject snapshot,
            std::int64_t updatedAt);
        void Apply(::HaloDesktop::Api::Dto::SettingsPayload payload);
        void SetString(wchar_t const* key, std::optional<winrt::hstring> value, bool debounce);
        void SetBoolean(wchar_t const* key, bool value);
        void Touch(bool debounce);
        [[nodiscard]] std::int64_t NextTimestamp() const noexcept;
        [[nodiscard]] winrt::Windows::Data::Json::JsonObject Snapshot() const;

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_apiClient;
        std::shared_ptr<QueryCache> m_queryCache;
        std::filesystem::path m_mirrorPath;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_debounceTimer{ nullptr };
        winrt::Windows::Data::Json::JsonObject m_value;
        std::int64_t m_updatedAt{};
        std::uint64_t m_writeVersion{};
        std::mutex m_mirrorMutex;
        std::int64_t m_mirrorWrittenAt{ -1 };
    };
}
