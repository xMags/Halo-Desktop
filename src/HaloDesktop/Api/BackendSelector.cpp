#include "pch.h"
#include "Api/BackendSelector.h"

#include "Api/ApiClient.h"
#include "Api/HaloBackend.h"

#include <stdexcept>
#include <utility>

namespace HaloDesktop::Api
{
    BackendSelector::BackendSelector(std::shared_ptr<ApiClient> account)
        : m_current(std::move(account))
    {
        if (!m_current)
        {
            throw std::invalid_argument{ "BackendSelector requires the account's API client." };
        }
    }

    std::shared_ptr<IHaloBackend> BackendSelector::Current() const
    {
        std::scoped_lock const lock{ m_mutex };
        return m_current;
    }
}
