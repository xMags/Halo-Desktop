#include "Services/SearchHistoryPolicy.h"

#include "Services/SearchResultsPolicy.h"

#include <algorithm>
#include <utility>

namespace HaloDesktop::Services
{
    std::vector<SearchHistoryEntry> AddSearchTerm(
        std::span<SearchHistoryEntry const> history,
        std::wstring_view term,
        std::int64_t now)
    {
        auto trimmed = TrimmedText(term);
        if (trimmed.size() < SearchMinimumQuery)
        {
            return { history.begin(), history.end() };
        }
        auto const lowered = LowerText(trimmed);
        std::vector<SearchHistoryEntry> result;
        result.reserve((std::min)(history.size() + 1, SearchHistoryLimit));
        result.push_back({ std::move(trimmed), now });
        for (auto const& entry : history)
        {
            if (result.size() == SearchHistoryLimit) break;
            if (LowerText(entry.Term) != lowered) result.push_back(entry);
        }
        return result;
    }

    std::vector<SearchHistoryEntry> RemoveSearchTerm(
        std::span<SearchHistoryEntry const> history,
        std::wstring_view term)
    {
        std::vector<SearchHistoryEntry> result;
        result.reserve(history.size());
        for (auto const& entry : history)
        {
            if (entry.Term != term) result.push_back(entry);
        }
        return result;
    }

    std::vector<std::wstring> SearchHistoryTerms(std::span<SearchHistoryEntry const> history)
    {
        std::vector<std::wstring> terms;
        terms.reserve(history.size());
        for (auto const& entry : history) terms.push_back(entry.Term);
        return terms;
    }
}
