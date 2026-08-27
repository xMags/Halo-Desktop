#pragma once

#include "Services/ServiceInterfaces.h"
#include "Services/StreamInfo.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace HaloDesktop::Api
{
    class ApiClient;
}

namespace HaloDesktop::Services
{
    class SettingsSyncService;

    struct ResolvedSourceRecord final
    {
        winrt::hstring Key;
        winrt::hstring AddonId;
        Api::Dto::StreamRecord Stream;
        ParsedStreamInfo Info;
        winrt::HaloDesktop::SourcesNavParams Navigation{ nullptr };
    };

    // UI-thread-only facade. Raw third-party URLs and request headers remain in
    // m_records and never enter observable or XAML-bound collections.
    class SourceService final : public ISourceService
    {
    public:
        SourceService(
            std::shared_ptr<Api::ApiClient> api,
            std::shared_ptr<IDownloadService> downloads,
            std::shared_ptr<SettingsSyncService> settings);

        [[nodiscard]] concurrency::task<void> LoadAsync(winrt::HaloDesktop::SourcesNavParams const& parameters) override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> Groups() const override;
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::StreamSource> Filter(winrt::hstring const& quality) const override;
        [[nodiscard]] winrt::HaloDesktop::StreamSource BestSource() const override;
        [[nodiscard]] winrt::HaloDesktop::PlaybackRequest BuildPlaybackRequest(winrt::hstring const& key) const override;
        [[nodiscard]] concurrency::task<DownloadStartOutcome> StartDownloadAsync(
            winrt::hstring key,
            bool replaceExisting) override;
        [[nodiscard]] winrt::hstring ResolveSummary() const override;
        [[nodiscard]] std::int32_t Count(winrt::hstring const& filter) const noexcept override;
        void RefreshDownloadStates() override;

        [[nodiscard]] std::optional<ResolvedSourceRecord> NativeRecord(winrt::hstring const& key) const;
        void OnAccountChanged();

    private:
        [[nodiscard]] bool Matches(winrt::HaloDesktop::StreamSource const& source, winrt::hstring const& filter) const noexcept;
        [[nodiscard]] concurrency::task<std::optional<Downloads::SubtitleRequest>> PrepareSubtitleAsync(
            Api::Dto::StreamRecord stream,
            winrt::hstring mediaType,
            winrt::hstring videoId,
            winrt::hstring preferredLanguage);

        std::shared_ptr<Api::ApiClient> m_api;
        std::shared_ptr<IDownloadService> m_downloads;
        std::shared_ptr<SettingsSyncService> m_settings;
        std::unordered_map<std::wstring, ResolvedSourceRecord> m_records;
        std::vector<winrt::hstring> m_orderedKeys;
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> m_groups{ nullptr };
        winrt::HaloDesktop::StreamSource m_best{ nullptr };
        winrt::hstring m_summary;
        std::uint64_t m_requestVersion{};
    };

    [[nodiscard]] winrt::hstring SanitizeAddonName(std::optional<winrt::hstring> const& name);
    [[nodiscard]] winrt::hstring AddonFailureCopy(std::optional<winrt::hstring> const& code);
}
