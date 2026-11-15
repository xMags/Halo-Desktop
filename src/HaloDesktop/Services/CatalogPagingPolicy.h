#pragma once

#include "ViewModels/HomeStatePolicy.h"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace HaloDesktop::Services
{
    // The reference client's `browse/catalogPaging.ts`. Addons serve a catalog
    // a page at a time, and the next page is asked for with the `skip` extra:
    // how many titles the addon has handed over so far.

    struct PagedTitle final
    {
        std::wstring Type;
        std::wstring Id;
    };

    // Where a "See all" grid gets more titles: the catalog, the extras the
    // shelf asked with (a language genre, kept on every page), the first page
    // as the addon sent it before any filtering (its length is the first
    // offset, and a repeat of it is how an addon that ignores `skip` is
    // caught), and the Home filter the grid keeps applying.
    struct CatalogPagingSource final
    {
        std::wstring AddonId;
        std::wstring Type;
        std::wstring CatalogId;
        std::vector<std::pair<std::wstring, std::wstring>> Extras;
        std::vector<PagedTitle> FirstPage;
        ::HaloDesktop::ViewModels::HomeFilter Filter{ ::HaloDesktop::ViewModels::HomeFilter::All };
    };

    using PagedTitleFilter = std::function<bool(PagedTitle const&)>;

    // The titles a new page adds to those shown, as indices into the page:
    // unseen ones the grid's filter keeps, in the addon's order. The same id
    // under another type is a different title.
    [[nodiscard]] std::vector<std::size_t> NewTitles(
        std::span<PagedTitle const> shown,
        std::span<PagedTitle const> page,
        PagedTitleFilter const& keep);

    // Whether to ask for another page after this one. An empty page is the end,
    // and so is a page of nothing but titles already received: an addon that
    // ignores `skip` answers every page with the first, which must stop the
    // paging rather than loop.
    [[nodiscard]] bool HasMorePages(
        std::span<PagedTitle const> received,
        std::span<PagedTitle const> page);

    // The running state of one grid's paging. Not thread-safe; the grid owns it
    // on the UI thread.
    class CatalogPager final
    {
    public:
        CatalogPager(std::span<PagedTitle const> firstPage, ::HaloDesktop::ViewModels::HomeFilter filter);

        // The `skip` the next page is asked with: every title the addon has
        // sent so far, filtered or not.
        [[nodiscard]] std::size_t NextSkip() const noexcept;
        [[nodiscard]] bool HasMore() const noexcept;
        // Takes one page and returns the indices of the titles it adds.
        [[nodiscard]] std::vector<std::size_t> Accept(std::span<PagedTitle const> page);

    private:
        ::HaloDesktop::ViewModels::HomeFilter m_filter;
        std::unordered_set<std::wstring> m_received;
        std::unordered_set<std::wstring> m_shown;
        std::size_t m_skip{};
        bool m_hasMore{ true };
    };
}
