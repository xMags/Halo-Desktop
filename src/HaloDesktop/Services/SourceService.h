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
        // Position in the whole resolve's ranking, ascending. Assigned after every
        // addon has answered, so it is comparable across addon groups.
        std::int32_t Rank{};
        // Set when this record is a file already on the device. Such a record is
        // played from disk; its Stream carries an addon's URL only when one also
        // returned the same release, as a fallback if the file has since gone.
        std::optional<winrt::hstring> DownloadJobId;
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
        [[nodiscard]] winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::StreamSource> LocalSources() const override;
        void RefreshDownloadStates() override;

        [[nodiscard]] std::optional<ResolvedSourceRecord> NativeRecord(winrt::hstring const& key) const;
        void OnAccountChanged();

    private:
        [[nodiscard]] bool Matches(winrt::HaloDesktop::StreamSource const& source, winrt::hstring const& filter) const noexcept;
        // Projects the retained resolve plus whatever is currently on disk into the
        // records, groups, local sources and ranking the sheet reads. Split out of
        // LoadAsync because a download finishing or being deleted has to reproduce
        // exactly this state without asking the providers again.
        void ApplyResolve();
        // Drops the retained resolve and everything projected from it. Used wherever
        // the service must stop answering for the title it last loaded.
        void ClearResolve();
        [[nodiscard]] concurrency::task<std::optional<Downloads::SubtitleRequest>> PrepareSubtitleAsync(
            Api::Dto::StreamRecord stream,
            winrt::hstring mediaType,
            winrt::hstring videoId,
            winrt::hstring preferredLanguage);

        std::shared_ptr<Api::ApiClient> m_api;
        std::shared_ptr<IDownloadService> m_downloads;
        std::shared_ptr<SettingsSyncService> m_settings;
        // The resolve is retained so ApplyResolve can run again without a second
        // request. Raw URLs and headers live here and in m_records only.
        winrt::HaloDesktop::SourcesNavParams m_parameters{ nullptr };
        Api::Dto::StreamsPayload m_payload;
        // One key per retained stream, in payload order. Minted once per resolve so
        // that a rebuild does not renumber the rows underneath an open sheet and
        // discard the viewer's selection.
        std::vector<std::vector<winrt::hstring>> m_streamKeys;
        double m_elapsedSeconds{};
        std::unordered_map<std::wstring, ResolvedSourceRecord> m_records;
        std::vector<winrt::hstring> m_orderedKeys;
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> m_groups{ nullptr };
        winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::StreamSource> m_localSources{ nullptr };
        winrt::HaloDesktop::StreamSource m_best{ nullptr };
        winrt::hstring m_summary;
        std::uint64_t m_requestVersion{};
    };

    [[nodiscard]] winrt::hstring SanitizeAddonName(std::optional<winrt::hstring> const& name);
    [[nodiscard]] winrt::hstring AddonFailureCopy(std::optional<winrt::hstring> const& code);
}
