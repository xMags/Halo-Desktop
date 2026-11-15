#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::Services
{
    // Device-local search history, most recent first, as the reference
    // client's `search/searchHistory.ts` keeps it. Pure rules; the store that
    // reads and writes the file is DevicePreferencesStore.

    inline constexpr std::size_t SearchHistoryLimit = 20;
    // Longer than any query the box accepts; a longer stored term is not ours.
    inline constexpr std::size_t SearchHistoryTermLimit = 120;

    struct SearchHistoryEntry final
    {
        std::wstring Term;
        // When the term was last searched, in milliseconds since the Unix
        // epoch. Zero for a term carried over from the older plain list.
        std::int64_t At{};

        bool operator==(SearchHistoryEntry const&) const = default;
    };

    // Moves (or inserts) the trimmed term at the front; a term shorter than
    // the search minimum leaves the history as it is. A case-insensitive
    // repeat is replaced, so the newest casing is kept.
    [[nodiscard]] std::vector<SearchHistoryEntry> AddSearchTerm(
        std::span<SearchHistoryEntry const> history,
        std::wstring_view term,
        std::int64_t now);
    // Removes exactly this term.
    [[nodiscard]] std::vector<SearchHistoryEntry> RemoveSearchTerm(
        std::span<SearchHistoryEntry const> history,
        std::wstring_view term);
    // The terms alone, in order.
    [[nodiscard]] std::vector<std::wstring> SearchHistoryTerms(std::span<SearchHistoryEntry const> history);
}
