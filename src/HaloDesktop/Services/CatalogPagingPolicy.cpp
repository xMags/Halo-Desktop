#include "Services/CatalogPagingPolicy.h"

namespace
{
    std::wstring KeyOf(::HaloDesktop::Services::PagedTitle const& title)
    {
        std::wstring key;
        key.reserve(title.Type.size() + 1 + title.Id.size());
        key.append(title.Type);
        key.push_back(L':');
        key.append(title.Id);
        return key;
    }

    bool KeepsKind(::HaloDesktop::ViewModels::HomeFilter filter, ::HaloDesktop::Services::PagedTitle const& title) noexcept
    {
        return ::HaloDesktop::ViewModels::MatchesHomeFilter(
            filter,
            title.Type == L"series"
                ? ::HaloDesktop::ViewModels::HomeMediaKind::Series
                : ::HaloDesktop::ViewModels::HomeMediaKind::Movie);
    }

    std::vector<std::size_t> CollectNew(
        std::unordered_set<std::wstring>& seen,
        std::span<::HaloDesktop::Services::PagedTitle const> page,
        ::HaloDesktop::Services::PagedTitleFilter const& keep)
    {
        std::vector<std::size_t> added;
        for (std::size_t index = 0; index < page.size(); ++index)
        {
            auto key = KeyOf(page[index]);
            if (seen.contains(key) || (keep && !keep(page[index])))
            {
                continue;
            }
            seen.insert(std::move(key));
            added.push_back(index);
        }
        return added;
    }

    bool AnyUnseen(
        std::unordered_set<std::wstring> const& received,
        std::span<::HaloDesktop::Services::PagedTitle const> page)
    {
        for (auto const& title : page)
        {
            if (!received.contains(KeyOf(title)))
            {
                return true;
            }
        }
        return false;
    }
}

namespace HaloDesktop::Services
{
    std::vector<std::size_t> NewTitles(
        std::span<PagedTitle const> shown,
        std::span<PagedTitle const> page,
        PagedTitleFilter const& keep)
    {
        std::unordered_set<std::wstring> seen;
        seen.reserve(shown.size() + page.size());
        for (auto const& title : shown)
        {
            seen.insert(KeyOf(title));
        }
        return CollectNew(seen, page, keep);
    }

    bool HasMorePages(std::span<PagedTitle const> received, std::span<PagedTitle const> page)
    {
        if (page.empty())
        {
            return false;
        }
        std::unordered_set<std::wstring> seen;
        seen.reserve(received.size());
        for (auto const& title : received)
        {
            seen.insert(KeyOf(title));
        }
        return AnyUnseen(seen, page);
    }

    CatalogPager::CatalogPager(std::span<PagedTitle const> firstPage, ::HaloDesktop::ViewModels::HomeFilter filter)
        : m_filter(filter), m_skip(firstPage.size())
    {
        for (auto const& title : firstPage)
        {
            auto key = KeyOf(title);
            if (KeepsKind(m_filter, title))
            {
                m_shown.insert(key);
            }
            m_received.insert(std::move(key));
        }
    }

    std::size_t CatalogPager::NextSkip() const noexcept { return m_skip; }

    bool CatalogPager::HasMore() const noexcept { return m_hasMore; }

    std::vector<std::size_t> CatalogPager::Accept(std::span<PagedTitle const> page)
    {
        m_hasMore = !page.empty() && AnyUnseen(m_received, page);
        m_skip += page.size();
        for (auto const& title : page)
        {
            m_received.insert(KeyOf(title));
        }
        auto const filter = m_filter;
        return CollectNew(m_shown, page, [filter](PagedTitle const& title) { return KeepsKind(filter, title); });
    }
}
