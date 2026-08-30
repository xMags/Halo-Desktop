#include "pch.h"
#include "Playback/UpNextResolver.h"

#include "Api/ApiClient.h"
#include "Models/Models.h"
#include "Services/SettingsSyncService.h"
#include "Services/ServiceInterfaces.h"
#include "Services/StreamInfo.h"

#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <winrt/Windows.System.Threading.h>

namespace
{
    winrt::hstring EpisodeLabel(HaloDesktop::Api::Dto::MetaVideo const& video)
    {
        if (!video.Season || !video.Episode)
        {
            return L"";
        }
        std::wostringstream label;
        label << L"S" << std::setw(2) << std::setfill(L'0') << *video.Season
              << L"E" << std::setw(2) << std::setfill(L'0') << *video.Episode;
        return winrt::hstring{ label.str() };
    }

    // One next-episode lookup, abandoned after fifteen seconds so a stalled addon
    // cannot hold up either the up-next card or the advance that follows it.
    concurrency::task<std::optional<HaloDesktop::Api::Dto::NextEpisodePayload>> FetchNextEpisodeAsync(
        HaloDesktop::Api::ApiClient& api,
        winrt::HaloDesktop::PlaybackRequest const& request)
    {
        using Payload = std::optional<HaloDesktop::Api::Dto::NextEpisodePayload>;
        concurrency::task_completion_event<Payload> completion;
        auto const claimed = std::make_shared<std::atomic_bool>(false);
        auto const timer = winrt::Windows::System::Threading::ThreadPoolTimer::CreateTimer(
            [claimed, completion](auto const&) mutable
            {
                if (!claimed->exchange(true))
                {
                    completion.set(std::nullopt);
                }
            },
            std::chrono::seconds(15));

        api.GetNextEpisodeAsync(
            request.MediaType(),
            request.MetaId(),
            request.VideoId(),
            request.AddonId(),
            request.BingeGroup()).then(
            [claimed, completion](concurrency::task<HaloDesktop::Api::Dto::NextEpisodePayload> task) mutable
            {
                try
                {
                    auto payload = task.get();
                    if (!claimed->exchange(true))
                    {
                        completion.set(std::move(payload));
                    }
                }
                catch (...)
                {
                    if (!claimed->exchange(true))
                    {
                        completion.set(std::nullopt);
                    }
                }
            });

        auto payload = co_await concurrency::create_task(completion);
        timer.Cancel();
        co_return payload;
    }

    winrt::HaloDesktop::PlaybackRequest BuildPlaybackRequest(
        winrt::HaloDesktop::PlaybackRequest const& request,
        HaloDesktop::Api::Dto::MetaVideo const& video,
        HaloDesktop::Api::Dto::StreamRecord const& stream)
    {
        namespace Services = HaloDesktop::Services;
        auto const info = Services::ParseStreamInfo(stream);
        return winrt::make<winrt::HaloDesktop::implementation::PlaybackRequest>(
            stream.Url,
            false,
            L"",
            L"",
            request.MediaType(),
            video.Id,
            request.ItemId(),
            request.MetaId(),
            video.Title,
            request.ShowName(),
            EpisodeLabel(video),
            request.Poster(),
            request.AddonId(),
            stream.BingeGroup.value_or(L""),
            info.Filename,
            info.SizeBytes.value_or(0),
            stream.VideoHash.value_or(L""),
            Services::BuildSourceTagLine(info),
            stream.RequestHeaders);
    }
}

namespace HaloDesktop::Playback
{
    UpNextResolver::UpNextResolver(
        std::shared_ptr<Api::ApiClient> api,
        std::shared_ptr<Services::SettingsSyncService> settings,
        std::shared_ptr<Services::IDownloadService> downloads)
        : m_api(std::move(api)), m_settings(std::move(settings)), m_downloads(std::move(downloads))
    {
        if (!m_api || !m_settings || !m_downloads)
        {
            throw std::invalid_argument("UpNextResolver requires its dependencies.");
        }
    }

    concurrency::task<std::optional<UpNextResult>> UpNextResolver::ResolveAsync(
        winrt::HaloDesktop::PlaybackRequest request)
    {
        if (!request || request.MediaType() != L"series")
        {
            co_return std::nullopt;
        }

        auto const uiContext = winrt::apartment_context{};
        try
        {
            co_await m_settings->LoadAsync();
        }
        catch (...)
        {
        }
        co_await uiContext;
        if (!m_settings->AutoplayNextEpisode())
        {
            co_return std::nullopt;
        }

        if (request.IsLocalFile())
        {
            auto const next = m_downloads->OfflineNext(request.DownloadId());
            if (!next)
            {
                co_return std::nullopt;
            }
            co_return UpNextResult{
                nullptr,
                *next,
                (*next).Title(),
                (*next).EpisodeLabel(),
                L"",
            };
        }

        auto const payload = co_await FetchNextEpisodeAsync(*m_api, request);
        if (!payload || !payload->Video)
        {
            co_return std::nullopt;
        }

        auto const& video = *payload->Video;
        auto const episodeLabel = EpisodeLabel(video);
        auto const sources = winrt::make<winrt::HaloDesktop::implementation::SourcesNavParams>(
            request.MediaType(),
            request.MetaId(),
            video.Id,
            request.ItemId(),
            video.Title,
            request.ShowName(),
            episodeLabel,
            request.Poster());

        // The stream this lookup may have returned is deliberately dropped. It
        // would be minutes to an hour old by the time anyone advanced, and addon
        // URLs are usually signed with a much shorter life than that.
        co_return UpNextResult{ sources, nullptr, video.Title, episodeLabel, video.Thumbnail.value_or(L"") };
    }

    concurrency::task<winrt::HaloDesktop::PlaybackRequest> UpNextResolver::ResolveNextPlaybackAsync(
        winrt::HaloDesktop::PlaybackRequest request)
    {
        if (!request || request.MediaType() != L"series" || request.IsLocalFile())
        {
            co_return nullptr;
        }
        auto const payload = co_await FetchNextEpisodeAsync(*m_api, request);
        if (!payload || !payload->Video || !payload->Stream)
        {
            co_return nullptr;
        }
        co_return BuildPlaybackRequest(request, *payload->Video, *payload->Stream);
    }
}
