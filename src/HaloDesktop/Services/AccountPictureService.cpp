#include "pch.h"
#include "Services/AccountPictureService.h"

#include "Api/ApiClient.h"

#include <stdexcept>
#include <utility>

namespace HaloDesktop::Services
{
    AccountPictureService::AccountPictureService(std::shared_ptr<Api::ApiClient> apiClient)
        : m_apiClient(std::move(apiClient))
    {
        if (!m_apiClient)
        {
            throw std::invalid_argument{ "AccountPictureService requires an API client." };
        }
    }

    concurrency::task<AccountPictureBytes> AccountPictureService::PictureAsync(winrt::hstring version)
    {
        if (version.empty())
        {
            co_return nullptr;
        }
        if (m_cachedVersion && *m_cachedVersion == version)
        {
            co_return m_cached;
        }
        if (!m_pending || !m_pendingVersion || *m_pendingVersion != version)
        {
            m_pendingVersion = version;
            m_pending = FetchAsync(version, m_generation);
        }
        auto pending = *m_pending;
        co_return co_await pending;
    }

    void AccountPictureService::OnAccountChanged() noexcept
    {
        ++m_generation;
        m_cachedVersion.reset();
        m_cached.reset();
        m_pendingVersion.reset();
        m_pending.reset();
    }

    concurrency::task<AccountPictureBytes> AccountPictureService::FetchAsync(
        winrt::hstring version,
        std::uint64_t generation)
    {
        auto const uiContext = winrt::apartment_context{};
        AccountPictureBytes picture;
        // Only an answer from the server is worth remembering: a picture, or
        // its word that there is none.
        auto answered = false;
        try
        {
            auto bytes = co_await m_apiClient->GetAvatarAsync();
            answered = true;
            if (bytes && !bytes->empty())
            {
                picture = std::make_shared<std::vector<std::uint8_t> const>(std::move(*bytes));
            }
        }
        catch (...)
        {
            picture.reset();
        }
        co_await uiContext;
        if (generation != m_generation)
        {
            co_return nullptr;
        }
        // A newer fingerprint replaced this request while it ran: its answer is
        // still returned to whoever asked, but not kept as the newer one's.
        if (m_pendingVersion && *m_pendingVersion == version)
        {
            m_pendingVersion.reset();
            m_pending.reset();
            if (answered)
            {
                m_cachedVersion = version;
                m_cached = picture;
            }
        }
        co_return picture;
    }
}
