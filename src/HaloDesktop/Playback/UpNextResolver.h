#pragma once

#include <memory>
#include <optional>
#include <ppltasks.h>
#include <winrt/HaloDesktop.h>

namespace HaloDesktop::Api
{
    class ApiClient;
}

namespace HaloDesktop::Services
{
    class IDownloadService;
    class SettingsSyncService;
}

namespace HaloDesktop::Playback
{
    struct UpNextResult final
    {
        winrt::HaloDesktop::SourcesNavParams Sources{ nullptr };
        winrt::HaloDesktop::PlaybackRequest Playback{ nullptr };
        winrt::hstring Title;
        winrt::hstring EpisodeLabel;
    };

    // Resolves only the server-authoritative metadata chain. Local-file
    // continuation is supplied by the download index in the download milestone.
    class UpNextResolver final
    {
    public:
        UpNextResolver(
            std::shared_ptr<Api::ApiClient> api,
            std::shared_ptr<Services::SettingsSyncService> settings,
            std::shared_ptr<Services::IDownloadService> downloads);

        [[nodiscard]] concurrency::task<std::optional<UpNextResult>> ResolveAsync(
            winrt::HaloDesktop::PlaybackRequest request);

    private:
        std::shared_ptr<Api::ApiClient> m_api;
        std::shared_ptr<Services::SettingsSyncService> m_settings;
        std::shared_ptr<Services::IDownloadService> m_downloads;
    };
}
