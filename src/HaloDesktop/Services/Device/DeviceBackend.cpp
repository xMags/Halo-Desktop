#include "pch.h"
#include "Services/Device/DeviceBackend.h"

#include "Api/Addons/AddonFetcher.h"
#include "Api/Addons/AddonResolver.h"
#include "Api/ApiError.h"
#include "Services/Device/DeviceRecordsPolicy.h"
#include "Services/Device/DeviceStore.h"

#include <algorithm>
#include <cwctype>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace
{
    namespace Json = winrt::Windows::Data::Json;
    namespace Addons = HaloDesktop::Api::Addons;
    namespace Device = HaloDesktop::Services::Device;
    namespace Dto = HaloDesktop::Api::Dto;
    namespace Mappers = HaloDesktop::Api::Mappers;

    // The server's refusal as the account's client reports it: the status in
    // the code and the `error` sentence as the message, cleaned as the HTTP
    // layer cleans a server's sentence (control characters to spaces, trimmed,
    // at most 512 characters).
    [[noreturn]] void ThrowApiError(std::uint16_t status, std::wstring_view sentence)
    {
        std::wstring message{ sentence };
        std::replace_if(message.begin(), message.end(), [](wchar_t character) { return std::iswcntrl(character) != 0; }, L' ');
        while (!message.empty() && std::iswspace(message.front()) != 0) message.erase(message.begin());
        while (!message.empty() && std::iswspace(message.back()) != 0) message.pop_back();
        if (message.size() > 512) message.resize(512);
        throw winrt::hresult_error{ ::HaloDesktop::Api::ApiError::MakeHttpStatus(status), winrt::hstring{ message } };
    }

    // One write's turn on its collection. It waits for the write before it and
    // hands over when it is destroyed, whatever happened. The previous turn's
    // task only ever completes (never with an exception), so awaiting it
    // cannot fail and an unawaited one is never an unobserved failure.
    class WriteTurn final
    {
    public:
        WriteTurn(std::mutex& mutex, std::optional<concurrency::task<void>>& tail)
        {
            std::scoped_lock const lock{ mutex };
            m_previous = tail;
            tail = concurrency::create_task(m_done);
        }

        ~WriteTurn()
        {
            m_done.set();
        }

        WriteTurn(WriteTurn const&) = delete;
        WriteTurn& operator=(WriteTurn const&) = delete;

        [[nodiscard]] std::optional<concurrency::task<void>> Previous() const
        {
            return m_previous;
        }

    private:
        std::optional<concurrency::task<void>> m_previous;
        concurrency::task_completion_event<void> m_done;
    };

    std::vector<Addons::ResolvableAddon> Resolvable(std::vector<Device::StoredAddon> const& addons)
    {
        std::vector<Addons::ResolvableAddon> result;
        result.reserve(addons.size());
        for (auto const& addon : addons)
        {
            result.push_back(Addons::MakeResolvableAddon(addon.Id, addon.TransportUrl, addon.Manifest));
        }
        return result;
    }

    Json::JsonObject SettingsJson(Json::JsonObject const& value, double updatedAt)
    {
        Json::JsonObject payload;
        payload.Insert(L"value", value);
        payload.Insert(L"updatedAt", Json::JsonValue::CreateNumberValue(updatedAt));
        return payload;
    }
}

namespace HaloDesktop::Services::Device
{
    DeviceBackend::DeviceBackend(DeviceBackendOptions options)
        : m_store(std::move(options.Store)),
          m_fetcher(std::move(options.Fetcher)),
          m_now(std::move(options.Now)),
          m_newId(std::move(options.NewId)),
          m_timeout(options.Timeout)
    {
        if (!m_store || !m_fetcher)
        {
            throw std::invalid_argument{ "DeviceBackend requires a document store and an addon fetcher." };
        }
        if (m_timeout.count() <= 0)
        {
            throw std::invalid_argument{ "DeviceBackend requires a positive addon budget." };
        }
    }

    DeviceBackend::WriteQueue& DeviceBackend::QueueFor(DeviceCollection collection) noexcept
    {
        switch (collection)
        {
        case DeviceCollection::Library: return m_queues[1];
        case DeviceCollection::WatchStates: return m_queues[2];
        case DeviceCollection::Settings: return m_queues[3];
        case DeviceCollection::Addons:
        case DeviceCollection::Profile:
            break;
        }
        return m_queues[0];
    }

    std::int64_t DeviceBackend::Now() const
    {
        if (m_now) return m_now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::wstring DeviceBackend::NewId() const
    {
        return m_newId ? m_newId() : NewDeviceProfileId();
    }

    concurrency::task<std::vector<StoredAddon>> DeviceBackend::ReadAddonsAsync()
    {
        auto const store = m_store;
        co_return ReadAddonsDocument(co_await store->ReadAsync(DeviceCollection::Addons));
    }

    concurrency::task<std::vector<Api::Dto::LibraryRow>> DeviceBackend::ReadLibraryAsync()
    {
        auto const store = m_store;
        co_return ReadLibraryDocument(co_await store->ReadAsync(DeviceCollection::Library));
    }

    concurrency::task<std::vector<Api::Dto::WatchEntry>> DeviceBackend::ReadWatchStatesAsync()
    {
        auto const store = m_store;
        co_return ReadWatchStatesDocument(co_await store->ReadAsync(DeviceCollection::WatchStates));
    }

    concurrency::task<std::optional<StoredSettings>> DeviceBackend::ReadSettingsAsync()
    {
        auto const store = m_store;
        co_return ReadSettingsDocument(co_await store->ReadAsync(DeviceCollection::Settings));
    }

    /* Addons */

    concurrency::task<Api::Dto::AddonsPayload> DeviceBackend::GetAddonsAsync()
    {
        auto const self = shared_from_this();
        auto const addons = co_await ReadAddonsAsync();
        co_return Mappers::ParseAddons(AddonsWire(addons));
    }

    concurrency::task<void> DeviceBackend::PutAddonsAsync(std::vector<winrt::hstring> transportUrls)
    {
        auto const self = shared_from_this();
        std::vector<std::wstring> urls;
        urls.reserve(transportUrls.size());
        for (auto const& url : transportUrls) urls.emplace_back(url);
        if (auto const problem = AddonUrlsProblem(urls)) ThrowApiError(400, *problem);

        auto& queue = QueueFor(DeviceCollection::Addons);
        WriteTurn const turn{ queue.Mutex, queue.Tail };
        if (auto previous = turn.Previous()) co_await *previous;

        auto const current = co_await ReadAddonsAsync();
        std::unordered_map<std::wstring, StoredAddon const*> byUrl;
        for (auto const& addon : current) byUrl.emplace(addon.TransportUrl, &addon);
        std::vector<std::wstring> fresh;
        std::copy_if(urls.begin(), urls.end(), std::back_inserter(fresh), [&byUrl](std::wstring const& url)
        {
            return !byUrl.contains(url);
        });

        auto const resolved = co_await Addons::FetchManifestsAsync(fresh, m_fetcher, m_timeout);
        if (resolved.Error) ThrowApiError(400, *resolved.Error);
        std::unordered_map<std::wstring, Json::JsonObject> fetched;
        for (auto const& [url, manifest] : resolved.Entries) fetched.insert_or_assign(url, manifest);

        auto const addedAt = Now();
        std::vector<StoredAddon> next;
        next.reserve(urls.size());
        for (auto const& url : urls)
        {
            if (auto const kept = byUrl.find(url); kept != byUrl.end())
            {
                next.push_back(*kept->second);
                continue;
            }
            next.push_back({ NewId(), url, fetched.at(url), false, addedAt });
        }
        co_await m_store->WriteAsync(DeviceCollection::Addons, WriteAddonsDocument(next));
    }

    concurrency::task<void> DeviceBackend::PatchAddonAsync(winrt::hstring addonId, bool hideCatalogs)
    {
        auto const self = shared_from_this();
        auto& queue = QueueFor(DeviceCollection::Addons);
        WriteTurn const turn{ queue.Mutex, queue.Tail };
        if (auto previous = turn.Previous()) co_await *previous;

        auto addons = co_await ReadAddonsAsync();
        auto const target = std::find_if(addons.begin(), addons.end(), [&addonId](StoredAddon const& addon)
        {
            return addon.Id == std::wstring_view{ addonId };
        });
        if (target == addons.end()) ThrowApiError(404, L"addon not found");
        target->HideCatalogs = hideCatalogs;
        co_await m_store->WriteAsync(DeviceCollection::Addons, WriteAddonsDocument(addons));
    }

    /* Resolution */

    concurrency::task<std::vector<Api::Dto::MetaPreview>> DeviceBackend::GetCatalogAsync(
        winrt::hstring addonId,
        winrt::hstring type,
        winrt::hstring catalogId,
        std::vector<std::pair<winrt::hstring, winrt::hstring>> extras)
    {
        auto const self = shared_from_this();
        if (addonId.empty() || type.empty() || catalogId.empty()) ThrowApiError(400, L"addon, type and id are required");
        Addons::ExtraArgs requested;
        requested.reserve(extras.size());
        for (auto const& [name, value] : extras) requested.emplace_back(std::wstring{ name }, std::wstring{ value });
        auto const extra = Addons::CatalogExtras(requested);
        if (auto const problem = Addons::CatalogExtraProblem(extra)) ThrowApiError(400, *problem);

        auto const addons = co_await ReadAddonsAsync();
        auto const entry = std::find_if(addons.begin(), addons.end(), [&addonId](StoredAddon const& addon)
        {
            return addon.Id == std::wstring_view{ addonId };
        });
        if (entry == addons.end()) ThrowApiError(403, L"addon not installed");

        Json::JsonObject answer{ nullptr };
        try
        {
            answer = co_await Addons::FetchCatalogAsync(
                Addons::MakeResolvableAddon(entry->Id, entry->TransportUrl, entry->Manifest),
                std::wstring{ type },
                std::wstring{ catalogId },
                extra,
                Addons::ResolveContext{ m_fetcher, m_timeout });
        }
        catch (...)
        {
            ThrowApiError(502, L"catalog fetch failed");
        }
        co_return Mappers::ParseCatalog(answer);
    }

    concurrency::task<Api::Dto::MetaDetail> DeviceBackend::GetMetaAsync(winrt::hstring type, winrt::hstring metaId)
    {
        auto const self = shared_from_this();
        if (type.empty() || metaId.empty()) ThrowApiError(400, L"type and id are required");
        auto const addons = co_await ReadAddonsAsync();
        auto const answer = co_await Addons::ResolveMetaWithCastAsync(
            Resolvable(addons), std::wstring{ type }, std::wstring{ metaId }, Addons::ResolveContext{ m_fetcher, m_timeout });
        if (!answer) ThrowApiError(404, L"no metadata found");
        co_return Mappers::ParseMeta(*answer);
    }

    concurrency::task<Api::Dto::StreamsPayload> DeviceBackend::GetStreamsAsync(winrt::hstring type, winrt::hstring videoId)
    {
        auto const self = shared_from_this();
        if (type.empty() || videoId.empty()) ThrowApiError(400, L"type and videoId are required");
        auto const addons = co_await ReadAddonsAsync();
        auto const answer = co_await Addons::ResolveStreamsAsync(
            Resolvable(addons), std::wstring{ type }, std::wstring{ videoId }, Addons::ResolveContext{ m_fetcher, m_timeout });
        co_return Mappers::ParseStreams(answer);
    }

    concurrency::task<Api::Dto::NextEpisodePayload> DeviceBackend::GetNextEpisodeAsync(
        winrt::hstring type,
        winrt::hstring metaId,
        winrt::hstring videoId,
        winrt::hstring addonId,
        winrt::hstring bingeGroup)
    {
        auto const self = shared_from_this();
        if (type.empty() || metaId.empty() || videoId.empty()) ThrowApiError(400, L"type, metaId and videoId are required");
        // Empty values are left out of the query, so the server never sees them.
        Addons::NextEpisodeQuery query{ std::wstring{ type }, std::wstring{ metaId }, std::wstring{ videoId }, std::nullopt, std::nullopt };
        if (!addonId.empty()) query.AddonId = std::wstring{ addonId };
        if (!bingeGroup.empty()) query.BingeGroup = std::wstring{ bingeGroup };
        if (query.BingeGroup && query.BingeGroup->size() > Addons::MaximumBingeGroupLength) ThrowApiError(400, L"bingeGroup too long");

        auto const addons = co_await ReadAddonsAsync();
        auto const answer = co_await Addons::ResolveNextEpisodeAsync(
            Resolvable(addons), std::move(query), Addons::ResolveContext{ m_fetcher, m_timeout });
        if (!answer) ThrowApiError(404, L"no metadata found");
        co_return Mappers::ParseNextEpisode(*answer);
    }

    concurrency::task<Api::Dto::SubtitlesPayload> DeviceBackend::GetSubtitlesAsync(
        winrt::hstring type,
        winrt::hstring videoId,
        std::optional<winrt::hstring> videoHash,
        std::optional<std::uint64_t> videoSize,
        std::optional<winrt::hstring> filename)
    {
        auto const self = shared_from_this();
        if (type.empty() || videoId.empty()) ThrowApiError(400, L"type and videoId are required");
        // The same omissions as the query string: empty values never reach the resolver.
        Addons::SubtitleQuery query;
        if (videoHash && !videoHash->empty()) query.VideoHash = std::wstring{ *videoHash };
        if (videoSize) query.VideoSize = *videoSize;
        if (filename && !filename->empty()) query.Filename = std::wstring{ *filename };
        if (auto const problem = Addons::SubtitleQueryProblem(query)) ThrowApiError(400, *problem);

        auto const addons = co_await ReadAddonsAsync();
        auto const answer = co_await Addons::ResolveSubtitlesAsync(
            Resolvable(addons), std::wstring{ type }, std::wstring{ videoId }, std::move(query),
            Addons::ResolveContext{ m_fetcher, m_timeout });
        co_return Mappers::ParseSubtitles(answer);
    }

    /* Library, watch history, settings */

    concurrency::task<std::vector<Api::Dto::LibraryRow>> DeviceBackend::GetLibraryAsync()
    {
        auto const self = shared_from_this();
        auto const rows = co_await ReadLibraryAsync();
        co_return Mappers::ParseLibrary(LibraryWire(rows));
    }

    concurrency::task<std::vector<Api::Dto::LibraryRow>> DeviceBackend::PutLibraryAsync(std::vector<Api::Dto::LibraryRow> rows)
    {
        auto const self = shared_from_this();
        if (auto const problem = LibraryProblem(rows)) ThrowApiError(400, *problem);
        auto& queue = QueueFor(DeviceCollection::Library);
        WriteTurn const turn{ queue.Mutex, queue.Tail };
        if (auto previous = turn.Previous()) co_await *previous;

        auto const merged = MergeLibrary(co_await ReadLibraryAsync(), rows);
        co_await m_store->WriteAsync(DeviceCollection::Library, WriteLibraryDocument(merged));
        co_return Mappers::ParseLibrary(LibraryWire(merged));
    }

    concurrency::task<std::vector<Api::Dto::WatchEntry>> DeviceBackend::GetWatchStateAsync()
    {
        auto const self = shared_from_this();
        auto const rows = co_await ReadWatchStatesAsync();
        co_return Mappers::ParseWatchState(WatchStatesWire(LiveWatchStates(rows)));
    }

    concurrency::task<std::vector<Api::Dto::WatchEntry>> DeviceBackend::GetStoredWatchStatesAsync()
    {
        auto const self = shared_from_this();
        co_return co_await ReadWatchStatesAsync();
    }

    concurrency::task<std::vector<Api::Dto::WatchEntry>> DeviceBackend::PutWatchStateAsync(std::vector<Api::Dto::WatchEntry> rows)
    {
        auto const self = shared_from_this();
        if (auto const problem = WatchStatesProblem(rows)) ThrowApiError(400, *problem);
        auto& queue = QueueFor(DeviceCollection::WatchStates);
        WriteTurn const turn{ queue.Mutex, queue.Tail };
        if (auto previous = turn.Previous()) co_await *previous;

        auto const merged = MergeWatchStates(co_await ReadWatchStatesAsync(), rows);
        co_await m_store->WriteAsync(DeviceCollection::WatchStates, WriteWatchStatesDocument(merged));
        co_return Mappers::ParseWatchState(WatchStatesWire(LiveWatchStates(merged)));
    }

    concurrency::task<Api::Dto::SettingsPayload> DeviceBackend::GetSettingsAsync()
    {
        auto const self = shared_from_this();
        auto const stored = co_await ReadSettingsAsync();
        if (!stored) co_return Api::Dto::SettingsPayload{ Json::JsonObject{}, 0 };
        co_return Mappers::ParseSettings(SettingsJson(stored->Value, stored->UpdatedAt));
    }

    concurrency::task<Api::Dto::SettingsPayload> DeviceBackend::PutSettingsAsync(Json::JsonObject value, std::int64_t updatedAt)
    {
        auto const self = shared_from_this();
        if (!value) ThrowApiError(400, L"settings must be an object");
        // A copy taken before anything is awaited: the caller's object is
        // never read again, and the answer matches what a later read returns.
        auto const ownValue = Json::JsonObject::Parse(value.Stringify());
        if (auto const problem = SettingsProblem(ownValue, updatedAt)) ThrowApiError(400, *problem);

        auto& queue = QueueFor(DeviceCollection::Settings);
        WriteTurn const turn{ queue.Mutex, queue.Tail };
        if (auto previous = turn.Previous()) co_await *previous;

        auto const stored = co_await ReadSettingsAsync();
        if (stored && static_cast<double>(updatedAt) <= stored->UpdatedAt)
        {
            co_return Mappers::ParseSettings(SettingsJson(stored->Value, stored->UpdatedAt));
        }
        auto const document = WriteSettingsDocument(ownValue, updatedAt);
        co_await m_store->WriteAsync(DeviceCollection::Settings, document);
        co_return Mappers::ParseSettings(SettingsJson(ownValue, static_cast<double>(updatedAt)));
    }
}
