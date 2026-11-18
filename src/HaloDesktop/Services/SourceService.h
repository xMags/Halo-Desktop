#pragma once

#include "Services/AddonNamePolicy.h"
#include "Services/ServiceInterfaces.h"
#include "Services/StreamInfo.h"
#include "ViewModels/SourcesModel.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace HaloDesktop::Api
{
    class ApiClient;
    class BackendSelector;
}

namespace HaloDesktop::Services
{
    class SettingsSyncService;

    // UI-thread-only facade. Raw third-party URLs and request headers stay in the
    // parsed resolve and never enter observable or XAML-bound collections.
    class SourceService final : public ISourceService
    {
    public:
        SourceService(
            std::shared_ptr<Api::ApiClient> api,
            std::shared_ptr<Api::BackendSelector> backends,
            std::shared_ptr<IDownloadService> downloads,
            std::shared_ptr<SettingsSyncService> settings);

        [[nodiscard]] concurrency::task<void> LoadAsync(
            winrt::HaloDesktop::SourcesNavParams const& parameters,
            StreamReuse reuse) override;
        [[nodiscard]] concurrency::task<StreamFetch> RequestStreamsAsync(
            winrt::hstring type,
            winrt::hstring videoId) override;
        [[nodiscard]] std::shared_ptr<Sources::ParsedResolve const> Current() const override;
        [[nodiscard]] winrt::HaloDesktop::PlaybackRequest BuildPlaybackRequest(winrt::hstring const& key) const override;
        [[nodiscard]] concurrency::task<DownloadStartResult> StartDownloadAsync(
            winrt::hstring key,
            bool replaceExisting) override;

        void OnAccountChanged();

    private:
        // Drops the current resolve. Used wherever the service must stop answering
        // for the title it last loaded.
        void ClearResolve();
        void Adopt(winrt::HaloDesktop::SourcesNavParams parameters, std::shared_ptr<Sources::ParsedResolve const> parsed);
        [[nodiscard]] Sources::ParsedStream const* Find(winrt::hstring const& key, winrt::hstring* addonId) const;
        // The one /streams request per video the title page and the sheet share:
        // the request already made for this video when it is still waiting or
        // answered recently, else a new one that replaces it.
        [[nodiscard]] concurrency::task<StreamFetch> SharedStreams(
            winrt::hstring const& type,
            winrt::hstring const& videoId,
            StreamReuse reuse);
        [[nodiscard]] concurrency::task<StreamFetch> FetchStreamsAsync(
            winrt::hstring type,
            winrt::hstring videoId,
            std::uint64_t serial);
        [[nodiscard]] concurrency::task<std::optional<Downloads::SubtitleRequest>> PrepareSubtitleAsync(
            Api::Dto::StreamRecord stream,
            winrt::hstring mediaType,
            winrt::hstring videoId,
            winrt::hstring preferredLanguage);

        // Not backend calls: the subtitle hash reads the stream itself, and the
        // addon proxy is account-only. Streams and subtitles go through the
        // current backend.
        std::shared_ptr<Api::ApiClient> m_api;
        std::shared_ptr<Api::BackendSelector> m_backends;
        std::shared_ptr<IDownloadService> m_downloads;
        std::shared_ptr<SettingsSyncService> m_settings;
        // The parameters the current resolve answered for; null while none does.
        winrt::HaloDesktop::SourcesNavParams m_parameters{ nullptr };
        std::shared_ptr<Sources::ParsedResolve const> m_current;
        // Stream key to its group and position in m_current.
        std::unordered_map<std::wstring, std::pair<std::size_t, std::size_t>> m_index;
        std::uint64_t m_requestVersion{};

        struct SharedFetch final
        {
            winrt::hstring Type;
            winrt::hstring VideoId;
            std::optional<concurrency::task<StreamFetch>> Task;
            // Set once the request answered; a failed one is dropped instead, so
            // the next caller asks again.
            std::optional<std::chrono::steady_clock::time_point> AnsweredAt;
            std::uint64_t Serial{};
        };
        // Only the newest video's request is kept: a title page asks for one
        // video at a time, and the sheet opens on the one the page showed.
        std::optional<SharedFetch> m_fetch;
        std::uint64_t m_fetchSerial{};
    };
}
