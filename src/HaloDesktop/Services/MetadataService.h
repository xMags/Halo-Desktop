#pragma once
#include "Services/ServiceInterfaces.h"
#include <memory>
namespace HaloDesktop::Api{class ApiClient;}
namespace HaloDesktop::Services{class WatchStateService;class MetadataService final:public IMetadataService{public:MetadataService(std::shared_ptr<::HaloDesktop::Api::ApiClient>,std::shared_ptr<WatchStateService>,std::shared_ptr<IDownloadService>);concurrency::task<void> LoadAsync(winrt::hstring,winrt::hstring)override;winrt::HaloDesktop::MediaDetail Detail()const override;winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::Episode> Episodes(std::int32_t)const override;private:std::shared_ptr<::HaloDesktop::Api::ApiClient>m_api;std::shared_ptr<WatchStateService>m_watch;std::shared_ptr<IDownloadService>m_downloads;winrt::HaloDesktop::MediaDetail m_detail{nullptr};std::vector<winrt::HaloDesktop::Episode>m_episodes;};}
