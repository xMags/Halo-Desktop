#include "pch.h"
#include "Services/DownloadArtworkService.h"

#include "Api/ApiClient.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace HaloDesktop::Services
{
    DownloadArtworkService::DownloadArtworkService(std::shared_ptr<Api::ApiClient> apiClient)
        : m_apiClient(std::move(apiClient))
    {
        if (!m_apiClient)
        {
            throw std::invalid_argument{ "DownloadArtworkService requires an API client." };
        }
    }

    concurrency::task<winrt::hstring> DownloadArtworkService::ResolveAsync(
        winrt::hstring type,
        winrt::hstring metaId,
        winrt::hstring videoId)
    {
        auto const metadata = co_await MetadataAsync(type, metaId);
        LandscapeArtworkSet artwork{
            .Background = metadata.Preview.Background.value_or(winrt::hstring{}),
        };
        for (auto const& video : metadata.Videos)
        {
            if (video.Thumbnail && !video.Thumbnail->empty())
            {
                artwork.Thumbnails.insert_or_assign(std::wstring{ video.Id }, *video.Thumbnail);
            }
        }
        co_return SelectLandscapeArtwork(videoId, artwork);
    }

    void DownloadArtworkService::OnAccountChanged()
    {
        std::scoped_lock const lock{ m_mutex };
        m_metadata.clear();
    }

    concurrency::task<Api::Dto::MetaDetail> DownloadArtworkService::MetadataAsync(
        winrt::hstring const& type,
        winrt::hstring const& metaId)
    {
        auto const key = std::wstring{ type } + L"\n" + std::wstring{ metaId };
        concurrency::task_completion_event<Api::Dto::MetaDetail> completion;
        concurrency::task<Api::Dto::MetaDetail> shared{ completion };
        {
            std::scoped_lock const lock{ m_mutex };
            if (auto const cached = m_metadata.find(key); cached != m_metadata.end())
            {
                return cached->second;
            }
            m_metadata.emplace(key, shared);
        }
        try
        {
            auto completionTask = m_apiClient->GetMetaAsync(type, metaId).then(
                [completion](concurrency::task<Api::Dto::MetaDetail> result) mutable
                {
                    try
                    {
                        static_cast<void>(completion.set(result.get()));
                    }
                    catch (...)
                    {
                        static_cast<void>(completion.set_exception(std::current_exception()));
                    }
                });
            static_cast<void>(completionTask);
        }
        catch (...)
        {
            static_cast<void>(completion.set_exception(std::current_exception()));
        }
        return shared;
    }
}
