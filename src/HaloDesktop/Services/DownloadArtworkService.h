#pragma once

#include "Api/Dto.h"
#include "Services/LandscapeArtworkPolicy.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <ppltasks.h>

namespace HaloDesktop::Api
{
    class ApiClient;
}

namespace HaloDesktop::Services
{
    // Resolves the landscape artwork stored beside a download's portrait poster.
    // Metadata requests are shared per title so several downloaded episodes do
    // not independently fetch the same series record.
    class DownloadArtworkService final
    {
    public:
        explicit DownloadArtworkService(std::shared_ptr<Api::ApiClient> apiClient);

        [[nodiscard]] concurrency::task<winrt::hstring> ResolveAsync(
            winrt::hstring type,
            winrt::hstring metaId,
            winrt::hstring videoId);
        void OnAccountChanged();

    private:
        [[nodiscard]] concurrency::task<Api::Dto::MetaDetail> MetadataAsync(
            winrt::hstring const& type,
            winrt::hstring const& metaId);

        std::shared_ptr<Api::ApiClient> m_apiClient;
        std::mutex m_mutex;
        std::unordered_map<std::wstring, concurrency::task<Api::Dto::MetaDetail>> m_metadata;
    };
}
