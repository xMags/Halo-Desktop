#pragma once

#include "Playback/IPlaybackEngine.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <ppltasks.h>
#include <winrt/HaloDesktop.h>

namespace HaloDesktop::Api { class ApiClient; }
namespace HaloDesktop::Services { class SettingsSyncService; }

namespace HaloDesktop::Playback
{
    struct AddonSubtitleDisplay final{winrt::hstring Key,Language,Addon,Variant;};

    // UI-thread-only subtitle pipeline. Provider URLs remain in the native map;
    // observable consumers receive opaque keys and display labels only.
    class SubtitleController final : public std::enable_shared_from_this<SubtitleController>
    {
    public:
        SubtitleController(std::shared_ptr<Api::ApiClient> api,std::shared_ptr<IPlaybackEngine> engine,std::shared_ptr<Services::SettingsSyncService> settings);
        ~SubtitleController();
        [[nodiscard]] concurrency::task<void> PrepareAsync(winrt::HaloDesktop::PlaybackRequest request);
        [[nodiscard]] concurrency::task<void> SelectAsync(winrt::hstring key,bool deliberate=true);
        [[nodiscard]] std::vector<AddonSubtitleDisplay> Choices()const;
        void SetChoicesChangedHandler(std::function<void()> handler);
        void Stop()noexcept;

    private:
        struct NativeChoice final{AddonSubtitleDisplay Display;winrt::hstring AddonId,SubtitleId,Url,Lang;};
        void ApplyStyle();
        void SweepExternalTracks();
        void OnEngineChanged();
        void ApplyRememberedOrDefault();
        void Remember(NativeChoice const& choice);
        [[nodiscard]] std::optional<winrt::hstring> PreferredKey()const;
        [[nodiscard]] concurrency::task<std::wstring> DownloadAsync(NativeChoice const& choice);

        std::shared_ptr<Api::ApiClient>m_api;std::shared_ptr<IPlaybackEngine>m_engine;std::shared_ptr<Services::SettingsSyncService>m_settings;
        winrt::HaloDesktop::PlaybackRequest m_request{nullptr};std::unordered_map<std::wstring,NativeChoice>m_choices;std::vector<AddonSubtitleDisplay>m_display;
        std::function<void()>m_changed;PlaybackChangedToken m_engineToken{};std::uint64_t m_appliedSerial{},m_generation{};bool m_selecting{};
    };

    [[nodiscard]] winrt::hstring SubtitleLanguageLabel(winrt::hstring code);
}
