#include "pch.h"
#include "Services/QueryCache.h"

#include <winrt/base.h>

namespace HaloDesktop::Services
{
    QueryCache::QueryCache()
        : m_ownerThread(std::this_thread::get_id())
    {
    }

    QueryCache::RequestId QueryCache::Issue(std::wstring_view key)
    {
        VerifyThread();
        auto const requestId = ++m_nextRequestId;
        m_entries[std::wstring{ key }].LatestIssued = requestId;
        return requestId;
    }

    bool QueryCache::IsLatest(std::wstring_view key, RequestId requestId) const
    {
        VerifyThread();
        auto const found = m_entries.find(std::wstring{ key });
        return found != m_entries.end() && found->second.LatestIssued == requestId;
    }

    void QueryCache::Invalidate(std::wstring_view key)
    {
        VerifyThread();
        m_entries.erase(std::wstring{ key });
    }

    void QueryCache::Clear()
    {
        VerifyThread();
        m_entries.clear();
    }

    void QueryCache::VerifyThread() const
    {
        if (std::this_thread::get_id() != m_ownerThread)
        {
            throw winrt::hresult_wrong_thread{};
        }
    }
}
