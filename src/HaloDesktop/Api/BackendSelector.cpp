#include "pch.h"
#include "Api/BackendSelector.h"

#include "Api/ApiClient.h"
#include "Api/HaloBackend.h"

#include <stdexcept>
#include <utility>

namespace HaloDesktop::Api
{
    BackendSelector::BackendSelector(std::shared_ptr<ApiClient> account)
        : m_account(std::move(account))
    {
        if (!m_account)
        {
            throw std::invalid_argument{ "BackendSelector requires the account's API client." };
        }
        m_current = m_account;
    }

    std::shared_ptr<IHaloBackend> BackendSelector::Current() const
    {
        std::scoped_lock const lock{ m_mutex };
        return m_current;
    }

    bool BackendSelector::UsesDevice() const
    {
        std::scoped_lock const lock{ m_mutex };
        return m_usesDevice;
    }

    void BackendSelector::UseDevice(std::shared_ptr<IHaloBackend> device)
    {
        if (!device)
        {
            throw std::invalid_argument{ "BackendSelector requires a device backend to switch to." };
        }
        // The backend being replaced is released after the lock, never under it.
        std::shared_ptr<IHaloBackend> previous;
        {
            std::scoped_lock const lock{ m_mutex };
            previous = std::exchange(m_current, std::move(device));
            m_usesDevice = true;
        }
    }

    void BackendSelector::UseAccount()
    {
        std::shared_ptr<IHaloBackend> previous;
        {
            std::scoped_lock const lock{ m_mutex };
            previous = std::exchange(m_current, std::shared_ptr<IHaloBackend>{ m_account });
            m_usesDevice = false;
        }
    }
}
