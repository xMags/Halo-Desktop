#pragma once

#include "Api/Addons/AddonClientPolicy.h"
#include "Api/HaloBackend.h"
#include "Services/Device/DeviceDocuments.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace HaloDesktop::Api::Addons
{
    class IAddonFetcher;
}

namespace HaloDesktop::Services::Device
{
    class IDeviceDocumentStore;

    struct DeviceBackendOptions final
    {
        std::shared_ptr<IDeviceDocumentStore> Store;
        // Addon traffic; the app passes an HttpAddonFetcher.
        std::shared_ptr<Api::Addons::IAddonFetcher> Fetcher;
        // Milliseconds since the epoch; the system clock when empty.
        std::function<std::int64_t()> Now;
        // New addon ids; random UUIDs when empty.
        std::function<std::wstring()> NewId;
        // Per-addon budget; the server's when not set.
        std::chrono::milliseconds Timeout{ Api::Addons::ResolveTimeout };
    };

    // Halo's backend for someone using it without an account (the
    // reference's `DeviceBackend`): the same contract ApiClient offers,
    // answered on this PC. Addons are asked directly through the shared
    // resolution rules (Api/Addons), and the addon list, library, watch
    // history and settings are kept in the document store with the server's
    // validation and last-write-wins rules. Every answer passes through the
    // same mappers as the server's, and a refusal is an hresult_error with the
    // server's status and sentence, so a service cannot tell the two apart. A
    // stored document that cannot be read is ApiError::DeviceDataUnreadable
    // and is left untouched: every write reads its document first and stops
    // there.
    //
    // There are no global addons on a device: GetAddonsAsync answers none.
    //
    // Writes to one collection run one at a time, each re-reading the stored
    // document first: the player reports progress on a timer and again on
    // pause, so two reports can overlap, and neither may overwrite the other.
    //
    // Must be owned by a std::shared_ptr: every call keeps the backend alive
    // until it completes. Thread-safe.
    class DeviceBackend final : public Api::IHaloBackend, public std::enable_shared_from_this<DeviceBackend>
    {
    public:
        explicit DeviceBackend(DeviceBackendOptions options);

        DeviceBackend(DeviceBackend const&) = delete;
        DeviceBackend& operator=(DeviceBackend const&) = delete;

        [[nodiscard]] concurrency::task<Api::Dto::AddonsPayload> GetAddonsAsync() override;
        // The server's diff contract: kept URLs keep their id and manifest
        // untouched, new URLs have their manifest fetched and validated all or
        // nothing, and URLs left out are removed.
        [[nodiscard]] concurrency::task<void> PutAddonsAsync(std::vector<winrt::hstring> transportUrls) override;
        [[nodiscard]] concurrency::task<void> PatchAddonAsync(winrt::hstring addonId, bool hideCatalogs) override;
        [[nodiscard]] concurrency::task<std::vector<Api::Dto::MetaPreview>> GetCatalogAsync(
            winrt::hstring addonId,
            winrt::hstring type,
            winrt::hstring catalogId,
            std::vector<std::pair<winrt::hstring, winrt::hstring>> extras = {}) override;
        [[nodiscard]] concurrency::task<Api::Dto::MetaDetail> GetMetaAsync(
            winrt::hstring type,
            winrt::hstring metaId) override;
        [[nodiscard]] concurrency::task<Api::Dto::StreamsPayload> GetStreamsAsync(
            winrt::hstring type,
            winrt::hstring videoId) override;
        [[nodiscard]] concurrency::task<Api::Dto::NextEpisodePayload> GetNextEpisodeAsync(
            winrt::hstring type,
            winrt::hstring metaId,
            winrt::hstring videoId,
            winrt::hstring addonId,
            winrt::hstring bingeGroup) override;
        [[nodiscard]] concurrency::task<Api::Dto::SubtitlesPayload> GetSubtitlesAsync(
            winrt::hstring type,
            winrt::hstring videoId,
            std::optional<winrt::hstring> videoHash,
            std::optional<std::uint64_t> videoSize,
            std::optional<winrt::hstring> filename) override;
        [[nodiscard]] concurrency::task<std::vector<Api::Dto::LibraryRow>> GetLibraryAsync() override;
        [[nodiscard]] concurrency::task<std::vector<Api::Dto::LibraryRow>> PutLibraryAsync(
            std::vector<Api::Dto::LibraryRow> rows) override;
        [[nodiscard]] concurrency::task<std::vector<Api::Dto::WatchEntry>> GetWatchStateAsync() override;
        [[nodiscard]] concurrency::task<std::vector<Api::Dto::WatchEntry>> PutWatchStateAsync(
            std::vector<Api::Dto::WatchEntry> rows) override;
        [[nodiscard]] concurrency::task<Api::Dto::SettingsPayload> GetSettingsAsync() override;
        [[nodiscard]] concurrency::task<Api::Dto::SettingsPayload> PutSettingsAsync(
            winrt::Windows::Data::Json::JsonObject value,
            std::int64_t updatedAt) override;

        // Every stored watch row, removal tombstones included, exactly as
        // stored: what moving into an account must send, so a removal made
        // here outlasts the account's older copy.
        [[nodiscard]] concurrency::task<std::vector<Api::Dto::WatchEntry>> GetStoredWatchStatesAsync();

    private:
        // The tail of one collection's writes. Each write takes a turn (see
        // WriteTurn in the .cpp), waits for the one before it and hands over
        // when it ends, whether it succeeded or not. The lock only guards the
        // tail and is never held across a suspension.
        struct WriteQueue final
        {
            std::mutex Mutex;
            std::optional<concurrency::task<void>> Tail;
        };

        [[nodiscard]] WriteQueue& QueueFor(DeviceCollection collection) noexcept;
        [[nodiscard]] concurrency::task<std::vector<StoredAddon>> ReadAddonsAsync();
        [[nodiscard]] concurrency::task<std::vector<Api::Dto::LibraryRow>> ReadLibraryAsync();
        [[nodiscard]] concurrency::task<std::vector<Api::Dto::WatchEntry>> ReadWatchStatesAsync();
        [[nodiscard]] concurrency::task<std::optional<StoredSettings>> ReadSettingsAsync();
        [[nodiscard]] std::int64_t Now() const;
        [[nodiscard]] std::wstring NewId() const;

        std::shared_ptr<IDeviceDocumentStore> m_store;
        std::shared_ptr<Api::Addons::IAddonFetcher> m_fetcher;
        std::function<std::int64_t()> m_now;
        std::function<std::wstring()> m_newId;
        std::chrono::milliseconds m_timeout;
        std::array<WriteQueue, 4> m_queues;
    };
}
