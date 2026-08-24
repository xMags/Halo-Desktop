#include "pch.h"
#include "Services/AddonService.h"

#include "Api/ApiClient.h"
#include "Models/Models.h"
#include "Services/QueryCache.h"

#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    constexpr wchar_t AddonsCacheKey[] = L"addons";

    winrt::hstring Trimmed(winrt::hstring const& input)
    {
        std::wstring value{ input };
        auto const first = std::find_if_not(value.begin(), value.end(), [](wchar_t character)
        {
            return std::iswspace(character) != 0;
        });
        auto const last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t character)
        {
            return std::iswspace(character) != 0;
        }).base();
        return first < last ? winrt::hstring{ std::wstring(first, last) } : winrt::hstring{};
    }

    winrt::hstring Initials(winrt::hstring const& name)
    {
        std::wstring result;
        auto newWord = true;
        for (auto const character : std::wstring_view{ name.c_str(), name.size() })
        {
            if (std::iswspace(character) != 0)
            {
                newWord = true;
                continue;
            }
            if (newWord)
            {
                result.push_back(static_cast<wchar_t>(std::towupper(character)));
                if (result.size() == 2)
                {
                    break;
                }
                newWord = false;
            }
        }
        if (result.empty())
        {
            result = L"AD";
        }
        return winrt::hstring{ result };
    }

    winrt::hstring Provides(::HaloDesktop::Api::Dto::AddonRecord const& addon)
    {
        std::wstring result;
        auto append = [&result](winrt::hstring const& value)
        {
            if (!result.empty())
            {
                result.append(L" · ");
            }
            result.append(value);
        };
        for (auto const& resource : addon.Resources)
        {
            append(resource);
        }
        if (result.empty())
        {
            for (auto const& type : addon.Types)
            {
                append(type);
            }
        }
        return result.empty() ? winrt::hstring{ L"Addon services" } : winrt::hstring{ result };
    }
}

namespace HaloDesktop::Services
{
    AddonService::AddonService(
        std::shared_ptr<::HaloDesktop::Api::ApiClient> apiClient,
        std::shared_ptr<QueryCache> queryCache,
        std::shared_ptr<ISessionService> session)
        : m_apiClient(std::move(apiClient)),
          m_queryCache(std::move(queryCache)),
          m_session(std::move(session)),
          m_items(winrt::single_threaded_observable_vector<winrt::HaloDesktop::Addon>())
    {
        if (!m_apiClient || !m_queryCache || !m_session)
        {
            throw std::invalid_argument{ "AddonService requires all dependencies." };
        }
    }

    winrt::Windows::Foundation::Collections::IObservableVector<winrt::HaloDesktop::Addon> AddonService::Items() const
    {
        return m_items;
    }

    std::vector<::HaloDesktop::Api::Dto::AddonRecord> AddonService::Records() const
    {
        return m_records;
    }

    bool AddonService::CanEditLists() const noexcept
    {
        return m_canEditLists;
    }

    concurrency::task<void> AddonService::LoadAsync()
    {
        auto const uiContext = winrt::apartment_context{};
        if (auto const cached = m_queryCache->TryGet<::HaloDesktop::Api::Dto::AddonsPayload>(AddonsCacheKey))
        {
            Apply(*cached);
            co_return;
        }

        auto const requestId = m_queryCache->Issue(AddonsCacheKey);
        auto payload = co_await m_apiClient->GetAddonsAsync();
        auto const shouldSeed = !m_seedAttempted && payload.Global.empty() && payload.User.empty();
        m_seedAttempted = true;
        if (shouldSeed)
        {
            co_await m_apiClient->PutAddonsAsync({
                L"https://v3-cinemeta.strem.io/manifest.json",
                L"https://opensubtitles-v3.strem.io/manifest.json",
            }, false);
            payload = co_await m_apiClient->GetAddonsAsync();
        }
        co_await uiContext;
        if (m_queryCache->Commit(AddonsCacheKey, requestId, payload, QueryTtl::Addons))
        {
            Apply(std::move(payload));
        }
    }

    concurrency::task<void> AddonService::AddAsync(winrt::hstring transportUrl)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const url = Trimmed(transportUrl);
        if (!m_canEditLists)
        {
            throw std::runtime_error{ "The addon list cannot be edited safely." };
        }
        winrt::Windows::Foundation::Uri uri{ url };
        if (url.empty() || uri.Host().empty()
            || (uri.SchemeName() != L"http" && uri.SchemeName() != L"https"))
        {
            throw std::invalid_argument{ "Enter a valid addon URL." };
        }
        for (auto const& record : m_records)
        {
            if (record.TransportUrl && *record.TransportUrl == url)
            {
                throw std::invalid_argument{ "That addon is already installed." };
            }
        }

        auto urls = TransportUrls(false);
        urls.push_back(url);
        co_await m_apiClient->PutAddonsAsync(std::move(urls), false);
        co_await uiContext;
        m_queryCache->Invalidate(AddonsCacheKey);
        co_await LoadAsync();
    }

    concurrency::task<void> AddonService::RemoveAsync(winrt::hstring addonId)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const found = std::find_if(m_records.begin(), m_records.end(), [&addonId](auto const& record)
        {
            return record.Id == addonId;
        });
        if (found == m_records.end() || !found->TransportUrl || !m_canEditLists
            || (found->IsGlobal && !m_session->IsAdmin()))
        {
            throw std::runtime_error{ "The addon cannot be removed safely." };
        }

        auto urls = TransportUrls(found->IsGlobal);
        std::erase(urls, *found->TransportUrl);
        co_await m_apiClient->PutAddonsAsync(std::move(urls), found->IsGlobal);
        co_await uiContext;
        m_queryCache->Invalidate(AddonsCacheKey);
        co_await LoadAsync();
    }

    concurrency::task<void> AddonService::SetCatalogsVisibleAsync(
        winrt::hstring addonId,
        bool visible)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const found = std::find_if(m_records.begin(), m_records.end(), [&addonId](auto const& record)
        {
            return record.Id == addonId;
        });
        if (found == m_records.end() || (found->IsGlobal && !m_session->IsAdmin()))
        {
            throw std::runtime_error{ "The addon cannot be changed." };
        }
        co_await m_apiClient->PatchAddonAsync(found->Id, found->IsGlobal, !visible);
        co_await uiContext;
        m_queryCache->Invalidate(AddonsCacheKey);
        co_await LoadAsync();
    }

    void AddonService::Apply(::HaloDesktop::Api::Dto::AddonsPayload payload)
    {
        m_records.clear();
        m_records.reserve(payload.Global.size() + payload.User.size());
        m_records.insert(m_records.end(), payload.Global.begin(), payload.Global.end());
        m_records.insert(m_records.end(), payload.User.begin(), payload.User.end());
        m_canEditLists = std::all_of(m_records.begin(), m_records.end(), [](auto const& record)
        {
            return record.TransportUrl.has_value();
        });

        m_items.Clear();
        for (auto const& record : m_records)
        {
            auto const canEdit = m_canEditLists && (!record.IsGlobal || m_session->IsAdmin());
            m_items.Append(winrt::make<winrt::HaloDesktop::implementation::Addon>(
                record.Id,
                record.TransportUrl.value_or(L""),
                Initials(record.Name),
                record.Name,
                record.Version,
                record.IsGlobal ? L"GLOBAL" : L"YOURS",
                Provides(record),
                canEdit,
                record.IsGlobal,
                !record.HideCatalogs));
        }
    }

    std::vector<winrt::hstring> AddonService::TransportUrls(bool global) const
    {
        std::vector<winrt::hstring> result;
        for (auto const& record : m_records)
        {
            if (record.IsGlobal == global)
            {
                if (!record.TransportUrl)
                {
                    throw std::runtime_error{ "The addon list cannot be edited safely." };
                }
                result.push_back(*record.TransportUrl);
            }
        }
        return result;
    }
}
