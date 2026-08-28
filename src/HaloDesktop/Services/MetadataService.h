#pragma once

#include "Services/ServiceInterfaces.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace HaloDesktop::Api
{
    class ApiClient;
}

namespace HaloDesktop::Services
{
    class WatchStateService;

    class MetadataService final : public IMetadataService
    {
    public:
        MetadataService(
            std::shared_ptr<::HaloDesktop::Api::ApiClient> api,
            std::shared_ptr<WatchStateService> watch,
            std::shared_ptr<IDownloadService> downloads);

        [[nodiscard]] concurrency::task<void> LoadAsync(
            winrt::hstring type,
            winrt::hstring metaId) override;
        [[nodiscard]] winrt::HaloDesktop::MediaDetail Detail() const override;
        [[nodiscard]] std::int32_t RuntimeMinutes() const noexcept override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Episode>
            Episodes(std::int32_t season) const override;
        void OnAccountChanged();

    private:
        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_api;
        std::shared_ptr<WatchStateService> m_watch;
        std::shared_ptr<IDownloadService> m_downloads;
        winrt::HaloDesktop::MediaDetail m_detail{ nullptr };
        std::int32_t m_runtimeMinutes{};
        std::vector<winrt::HaloDesktop::Episode> m_episodes;
        // Prevent an older detail response from replacing the current title.
        std::uint64_t m_requestVersion{};
    };
}
