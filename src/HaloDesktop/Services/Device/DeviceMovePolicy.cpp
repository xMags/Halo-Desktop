#include "pch.h"
#include "Services/Device/DeviceMovePolicy.h"

#include "Api/ApiError.h"
#include "Api/HaloBackend.h"
#include "Services/AddonListPolicy.h"

#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace
{
    namespace Dto = HaloDesktop::Api::Dto;

    std::vector<winrt::hstring> OwnUrls(std::vector<Dto::AddonRecord> const& addons)
    {
        std::vector<winrt::hstring> urls;
        for (auto const& addon : addons)
        {
            if (addon.TransportUrl) urls.push_back(*addon.TransportUrl);
        }
        return urls;
    }

    bool IsManifestRefusal(winrt::hresult_error const& error) noexcept
    {
        auto const status = ::HaloDesktop::Api::ApiError::HttpStatus(error.code());
        return status && *status == 400;
    }
}

namespace HaloDesktop::Services::Device
{
    std::vector<AddonMove> AddonsToMove(
        std::span<Api::Dto::AddonRecord const> device,
        std::span<Api::Dto::AddonRecord const> accountOwn)
    {
        std::unordered_set<std::wstring> present;
        for (auto const& addon : accountOwn)
        {
            if (addon.TransportUrl) present.emplace(*addon.TransportUrl);
        }
        std::vector<AddonMove> moves;
        for (auto const& addon : device)
        {
            if (!addon.TransportUrl || addon.TransportUrl->empty()) continue;
            std::wstring url{ *addon.TransportUrl };
            if (::HaloDesktop::Services::IsDefaultAddonUrl(url) || present.contains(url)) continue;
            moves.push_back({ std::move(url), std::wstring{ addon.Name }, addon.HideCatalogs });
        }
        return moves;
    }

    bool ShouldMoveSettings(std::int64_t deviceUpdatedAt, std::int64_t accountUpdatedAt) noexcept
    {
        return accountUpdatedAt == 0 && deviceUpdatedAt > 0;
    }

    concurrency::task<MoveResult> MoveRecordsIntoAccountAsync(
        std::shared_ptr<Api::IHaloBackend> device,
        StoredWatchStatesReader storedWatchStates,
        std::shared_ptr<Api::IHaloBackend> account)
    {
        if (!device || !storedWatchStates || !account)
        {
            throw std::invalid_argument{ "Moving this PC's data needs both sides." };
        }

        for (auto& chunk : Chunked(co_await device->GetLibraryAsync()))
        {
            static_cast<void>(co_await account->PutLibraryAsync(std::move(chunk)));
        }
        for (auto& chunk : Chunked(co_await storedWatchStates()))
        {
            static_cast<void>(co_await account->PutWatchStateAsync(std::move(chunk)));
        }

        auto const deviceSettings = co_await device->GetSettingsAsync();
        auto const accountSettings = co_await account->GetSettingsAsync();
        if (ShouldMoveSettings(deviceSettings.UpdatedAt, accountSettings.UpdatedAt))
        {
            static_cast<void>(co_await account->PutSettingsAsync(deviceSettings.Value, deviceSettings.UpdatedAt));
        }

        auto const deviceAddons = co_await device->GetAddonsAsync();
        auto accountAddons = co_await account->GetAddonsAsync();
        auto own = OwnUrls(accountAddons.User);
        MoveResult result;
        for (auto const& move : AddonsToMove(deviceAddons.User, accountAddons.User))
        {
            auto urls = own;
            urls.emplace_back(move.TransportUrl);
            auto refused = false;
            try
            {
                co_await account->PutAddonsAsync(std::move(urls));
            }
            catch (winrt::hresult_error const& error)
            {
                if (!IsManifestRefusal(error)) throw;
                refused = true;
            }
            if (refused)
            {
                result.FailedAddons.push_back(move.Name);
                continue;
            }
            // The account's PUT answers nothing here, so its list is read back
            // for the new entry's id and the next addon's base.
            accountAddons = co_await account->GetAddonsAsync();
            own = OwnUrls(accountAddons.User);
            if (!move.HideCatalogs) continue;
            auto const entry = std::find_if(accountAddons.User.begin(), accountAddons.User.end(), [&move](Dto::AddonRecord const& addon)
            {
                return addon.TransportUrl && std::wstring_view{ *addon.TransportUrl } == move.TransportUrl;
            });
            if (entry != accountAddons.User.end())
            {
                co_await account->PatchAddonAsync(entry->Id, true);
            }
        }
        co_return result;
    }

    AccountChangedDuringMove::AccountChangedDuringMove()
        : std::runtime_error{ "The signed-in account changed while this PC's data was moving." }
    {
    }

    OwnerCheckedBackend::OwnerCheckedBackend(std::shared_ptr<Api::IHaloBackend> inner, std::function<bool()> isStillOwner)
        : m_inner(std::move(inner)),
          m_isStillOwner(std::move(isStillOwner))
    {
        if (!m_inner || !m_isStillOwner)
        {
            throw std::invalid_argument{ "An owner-checked backend needs a backend and an owner check." };
        }
    }

    void OwnerCheckedBackend::RequireOwner() const
    {
        if (!m_isStillOwner()) throw AccountChangedDuringMove{};
    }

    concurrency::task<Api::Dto::AddonsPayload> OwnerCheckedBackend::GetAddonsAsync()
    {
        RequireOwner();
        return m_inner->GetAddonsAsync();
    }

    concurrency::task<void> OwnerCheckedBackend::PutAddonsAsync(std::vector<winrt::hstring> transportUrls)
    {
        RequireOwner();
        return m_inner->PutAddonsAsync(std::move(transportUrls));
    }

    concurrency::task<void> OwnerCheckedBackend::PatchAddonAsync(winrt::hstring addonId, bool hideCatalogs)
    {
        RequireOwner();
        return m_inner->PatchAddonAsync(std::move(addonId), hideCatalogs);
    }

    concurrency::task<std::vector<Api::Dto::MetaPreview>> OwnerCheckedBackend::GetCatalogAsync(
        winrt::hstring addonId,
        winrt::hstring type,
        winrt::hstring catalogId,
        std::vector<std::pair<winrt::hstring, winrt::hstring>> extras)
    {
        RequireOwner();
        return m_inner->GetCatalogAsync(std::move(addonId), std::move(type), std::move(catalogId), std::move(extras));
    }

    concurrency::task<Api::Dto::MetaDetail> OwnerCheckedBackend::GetMetaAsync(winrt::hstring type, winrt::hstring metaId)
    {
        RequireOwner();
        return m_inner->GetMetaAsync(std::move(type), std::move(metaId));
    }

    concurrency::task<Api::Dto::StreamsPayload> OwnerCheckedBackend::GetStreamsAsync(winrt::hstring type, winrt::hstring videoId)
    {
        RequireOwner();
        return m_inner->GetStreamsAsync(std::move(type), std::move(videoId));
    }

    concurrency::task<Api::Dto::NextEpisodePayload> OwnerCheckedBackend::GetNextEpisodeAsync(
        winrt::hstring type,
        winrt::hstring metaId,
        winrt::hstring videoId,
        winrt::hstring addonId,
        winrt::hstring bingeGroup)
    {
        RequireOwner();
        return m_inner->GetNextEpisodeAsync(
            std::move(type), std::move(metaId), std::move(videoId), std::move(addonId), std::move(bingeGroup));
    }

    concurrency::task<Api::Dto::SubtitlesPayload> OwnerCheckedBackend::GetSubtitlesAsync(
        winrt::hstring type,
        winrt::hstring videoId,
        std::optional<winrt::hstring> videoHash,
        std::optional<std::uint64_t> videoSize,
        std::optional<winrt::hstring> filename)
    {
        RequireOwner();
        return m_inner->GetSubtitlesAsync(
            std::move(type), std::move(videoId), std::move(videoHash), videoSize, std::move(filename));
    }

    concurrency::task<std::vector<Api::Dto::LibraryRow>> OwnerCheckedBackend::GetLibraryAsync()
    {
        RequireOwner();
        return m_inner->GetLibraryAsync();
    }

    concurrency::task<std::vector<Api::Dto::LibraryRow>> OwnerCheckedBackend::PutLibraryAsync(std::vector<Api::Dto::LibraryRow> rows)
    {
        RequireOwner();
        return m_inner->PutLibraryAsync(std::move(rows));
    }

    concurrency::task<std::vector<Api::Dto::WatchEntry>> OwnerCheckedBackend::GetWatchStateAsync()
    {
        RequireOwner();
        return m_inner->GetWatchStateAsync();
    }

    concurrency::task<std::vector<Api::Dto::WatchEntry>> OwnerCheckedBackend::PutWatchStateAsync(std::vector<Api::Dto::WatchEntry> rows)
    {
        RequireOwner();
        return m_inner->PutWatchStateAsync(std::move(rows));
    }

    concurrency::task<Api::Dto::SettingsPayload> OwnerCheckedBackend::GetSettingsAsync()
    {
        RequireOwner();
        return m_inner->GetSettingsAsync();
    }

    concurrency::task<Api::Dto::SettingsPayload> OwnerCheckedBackend::PutSettingsAsync(
        winrt::Windows::Data::Json::JsonObject value,
        std::int64_t updatedAt)
    {
        RequireOwner();
        return m_inner->PutSettingsAsync(std::move(value), updatedAt);
    }

    std::wstring FailedAddonsTitle(std::size_t count)
    {
        return count == 1 ? L"An addon was not moved" : L"Some addons were not moved";
    }

    std::wstring FailedAddonsBody(std::vector<std::wstring> const& names)
    {
        std::wstring list;
        for (auto const& name : names)
        {
            if (!list.empty()) list += L", ";
            list += name;
        }
        auto const one = names.size() == 1;
        return list + L" could not be added to your account because " + (one ? L"it" : L"they")
            + L" did not load. You can add " + (one ? L"it" : L"them") + L" again in Settings.";
    }
}
