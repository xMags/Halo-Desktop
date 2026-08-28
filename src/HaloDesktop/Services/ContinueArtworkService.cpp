#include "pch.h"
#include "Services/ContinueArtworkService.h"

#include "Api/ApiClient.h"
#include "Models/Models.h"

#include <pplawait.h>
#include <stdexcept>
#include <utility>
#include <wil/cppwinrt_helpers.h>

namespace HaloDesktop::Services
{
    ContinueArtworkService::ContinueArtworkService(
        std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_apiClient(std::move(apiClient)), m_dispatcher(std::move(dispatcher))
    {
        if (!m_apiClient || !m_dispatcher)
        {
            throw std::invalid_argument{ "ContinueArtworkService requires an API client and a dispatcher." };
        }
    }

    std::uint64_t ContinueArtworkService::BeginGeneration() noexcept
    {
        return ++m_generation;
    }

    concurrency::task<void> ContinueArtworkService::FillAsync(
        std::vector<winrt::HaloDesktop::ContinueItem> items,
        std::uint64_t generation)
    {
        // The cache and the generation stamp belong to the dispatcher thread, and
        // a caller is not required to already be on it.
        if (!m_dispatcher.HasThreadAccess())
        {
            co_await wil::resume_foreground(m_dispatcher);
        }
        std::vector<concurrency::task<void>> lookups;
        lookups.reserve(items.size());
        for (auto const& item : items)
        {
            lookups.push_back(FillItemAsync(item, generation));
        }
        // One item's meta failing says nothing about the rest, and a card that keeps
        // its poster is the behaviour this replaced rather than a broken card.
        for (auto& lookup : lookups)
        {
            try
            {
                co_await lookup;
            }
            catch (...)
            {
            }
        }
    }

    concurrency::task<void> ContinueArtworkService::FillItemAsync(
        winrt::HaloDesktop::ContinueItem item,
        std::uint64_t generation)
    {
        if (!item || item.MetaId().empty() || item.Type().empty())
        {
            co_return;
        }

        // Holds the service up for as long as the lookup runs, so the resumption
        // below cannot land on a destroyed cache during shutdown.
        auto const lifetime = shared_from_this();
        auto const key = std::wstring{ item.ItemId() };
        if (auto const cached = m_cache.find(key); cached != m_cache.end())
        {
            // No await on this path, so a revisited shelf is already correct by the
            // time its cards are built.
            Apply(item, cached->second);
            co_return;
        }

        MetaArtwork artwork;
        try
        {
            auto const meta = co_await m_apiClient->GetMetaAsync(item.Type(), item.MetaId());
            artwork.Background = meta.Preview.Background.value_or(winrt::hstring{});
            for (auto const& video : meta.Videos)
            {
                if (video.Thumbnail && !video.Thumbnail->empty())
                {
                    artwork.Thumbnails.insert_or_assign(std::wstring{ video.Id }, *video.Thumbnail);
                }
            }
        }
        catch (...)
        {
            // Cached empty below, which leaves the card on its poster.
        }

        co_await wil::resume_foreground(m_dispatcher);
        if (generation != m_generation)
        {
            co_return;
        }
        auto const& stored = m_cache.insert_or_assign(key, std::move(artwork)).first->second;
        Apply(item, stored);
    }

    void ContinueArtworkService::Apply(
        winrt::HaloDesktop::ContinueItem const& item,
        MetaArtwork const& artwork)
    {
        // The episode still beats the backdrop: it is the frame from the episode the
        // viewer is partway through, not a picture of the show in general.
        auto const thumbnail = artwork.Thumbnails.find(std::wstring{ item.VideoId() });
        auto const still = thumbnail != artwork.Thumbnails.end() ? thumbnail->second : artwork.Background;
        if (still.empty())
        {
            return;
        }
        winrt::get_self<winrt::HaloDesktop::implementation::ContinueItem>(item)->SetStill(still);
    }

    void ContinueArtworkService::OnAccountChanged() noexcept
    {
        ++m_generation;
        m_cache.clear();
    }
}
