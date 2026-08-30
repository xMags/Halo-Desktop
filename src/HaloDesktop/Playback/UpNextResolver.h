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
        // Only a downloaded continuation is playable straight from this result,
        // because a local file does not go stale. An addon stream is deliberately
        // absent: it is resolved again at the moment of the advance.
        winrt::HaloDesktop::PlaybackRequest Playback{ nullptr };
        winrt::hstring Title;
        winrt::hstring EpisodeLabel;
        // The next episode's own still, when the addon supplies one. Empty for a
        // downloaded continuation, which is resolved from the local index alone.
        winrt::hstring Still;
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

        // Called when playback starts, to describe what comes next.
        [[nodiscard]] concurrency::task<std::optional<UpNextResult>> ResolveAsync(
            winrt::HaloDesktop::PlaybackRequest request);

        // Called when the advance actually happens, which can be a whole episode
        // after ResolveAsync. Addons hand out short-lived stream URLs, so the one
        // fetched at playback start is often expired by then; this asks again and
        // returns nullptr when no playable stream comes back.
        [[nodiscard]] concurrency::task<winrt::HaloDesktop::PlaybackRequest> ResolveNextPlaybackAsync(
            winrt::HaloDesktop::PlaybackRequest request);

    private:
        std::shared_ptr<Api::ApiClient> m_api;
        std::shared_ptr<Services::SettingsSyncService> m_settings;
        std::shared_ptr<Services::IDownloadService> m_downloads;
    };
}
