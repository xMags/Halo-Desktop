#include "Services/HomeRowsPolicy.h"

#include "Services/ContinueShelfPolicy.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>

namespace
{
    // What JavaScript's `\s` matches: the ASCII and Unicode spaces and line
    // breaks, and the byte-order mark.
    bool IsJsSpace(wchar_t value) noexcept
    {
        switch (value)
        {
        case L'\t': case L'\n': case L'\v': case L'\f': case L'\r': case L' ':
        case 0x00A0: case 0x1680: case 0x2028: case 0x2029: case 0x202F:
        case 0x205F: case 0x3000: case 0xFEFF:
            return true;
        default:
            return value >= 0x2000 && value <= 0x200A;
        }
    }

    // JavaScript's `\w`, which is what its `\b` boundaries are made of.
    bool IsAsciiWordCharacter(wchar_t value) noexcept
    {
        return (value >= L'a' && value <= L'z') || (value >= L'A' && value <= L'Z')
            || (value >= L'0' && value <= L'9') || value == L'_';
    }

    std::wstring Trim(std::wstring_view value)
    {
        auto begin = value.begin();
        auto end = value.end();
        while (begin != end && IsJsSpace(*begin)) ++begin;
        while (end != begin && IsJsSpace(*(end - 1))) --end;
        return std::wstring{ begin, end };
    }

    // Unicode case mapping, as JavaScript's toLowerCase / toUpperCase do it,
    // rather than the C runtime's, which leaves non-ASCII letters alone.
    std::wstring MapCase(std::wstring_view value, DWORD flags)
    {
        if (value.empty())
        {
            return {};
        }
        auto const length = static_cast<int>((std::min)(value.size(), static_cast<std::size_t>(0x7FFFFFFF)));
        auto const needed = ::LCMapStringEx(
            LOCALE_NAME_INVARIANT, flags, value.data(), length, nullptr, 0, nullptr, nullptr, 0);
        if (needed <= 0)
        {
            return std::wstring{ value };
        }
        std::wstring result(static_cast<std::size_t>(needed), L'\0');
        auto const written = ::LCMapStringEx(
            LOCALE_NAME_INVARIANT, flags, value.data(), length, result.data(), needed, nullptr, nullptr, 0);
        if (written <= 0)
        {
            return std::wstring{ value };
        }
        result.resize(static_cast<std::size_t>(written));
        return result;
    }

    std::wstring Lower(std::wstring_view value) { return MapCase(value, LCMAP_LOWERCASE); }
    std::wstring Upper(std::wstring_view value) { return MapCase(value, LCMAP_UPPERCASE); }

    // ASCII-only folding for the kind words and the addon suffix, which are
    // ASCII patterns matched case-insensitively.
    wchar_t FoldAscii(wchar_t value) noexcept
    {
        return value >= L'A' && value <= L'Z' ? static_cast<wchar_t>(value - L'A' + L'a') : value;
    }

    bool EqualsAsciiFolded(std::wstring_view text, std::size_t at, std::wstring_view word) noexcept
    {
        if (at + word.size() > text.size())
        {
            return false;
        }
        for (std::size_t index = 0; index < word.size(); ++index)
        {
            if (FoldAscii(text[at + index]) != word[index])
            {
                return false;
            }
        }
        return true;
    }

    // `\b(word)\b`, case-insensitively, for any of the given words.
    bool ContainsWholeWord(std::wstring_view text, std::span<std::wstring_view const> words) noexcept
    {
        for (std::size_t at = 0; at < text.size(); ++at)
        {
            if (at > 0 && IsAsciiWordCharacter(text[at - 1]))
            {
                continue;
            }
            for (auto const word : words)
            {
                auto const end = at + word.size();
                if (EqualsAsciiFolded(text, at, word) && (end == text.size() || !IsAsciiWordCharacter(text[end])))
                {
                    return true;
                }
            }
        }
        return false;
    }

    constexpr std::array<std::wstring_view, 5> MovieKindWords{ L"movie", L"movies", L"film", L"films", L"cinema" };
    constexpr std::array<std::wstring_view, 4> SeriesKindWords{ L"series", L"show", L"shows", L"tv" };

    constexpr std::array<std::wstring_view, 10> TrailingNouns{
        L"additions", L"arrivals", L"favorites", L"favourites", L"gems",
        L"hits", L"picks", L"premieres", L"releases", L"titles",
    };

    struct KindLabels final
    {
        std::wstring Plural;
        std::wstring Singular;
    };

    KindLabels LabelsForKind(std::wstring_view type)
    {
        if (type == L"movie") return { L"Movies", L"Movie" };
        if (type == L"series") return { L"Series", L"Series" };
        std::wstring word{ type };
        if (!word.empty())
        {
            word = Upper(word.substr(0, 1)) + word.substr(1);
        }
        return { word, word };
    }

    // `\s*\badd-?on$`, case-insensitively: "Addon" or "Add-on" ending a name,
    // with the spaces before it.
    std::wstring WithoutAddonSuffix(std::wstring_view name)
    {
        for (auto const suffix : { std::wstring_view{ L"add-on" }, std::wstring_view{ L"addon" } })
        {
            if (name.size() < suffix.size())
            {
                continue;
            }
            auto const at = name.size() - suffix.size();
            if (!EqualsAsciiFolded(name, at, suffix) || (at > 0 && IsAsciiWordCharacter(name[at - 1])))
            {
                continue;
            }
            auto start = at;
            while (start > 0 && IsJsSpace(name[start - 1])) --start;
            return std::wstring{ name.substr(0, start) };
        }
        return std::wstring{ name };
    }

    bool IsSeparatorDash(wchar_t value) noexcept
    {
        // Hyphen, en dash, em dash and bar.
        return value == L'-' || value == L'\u2013' || value == L'\u2014' || value == L'|';
    }

    // Where the rest begins when a separator starts exactly at `at`, following
    // `(?:\s+[-\u2013\u2014|]\s+|:\s+)(.+)$`: the trailing spaces are greedy but leave at
    // least one character for the rest.
    std::optional<std::size_t> RestAfterSeparator(std::wstring_view name, std::size_t at) noexcept
    {
        auto const restAfterSpaces = [&name](std::size_t spacesStart) -> std::optional<std::size_t>
        {
            auto end = spacesStart;
            while (end < name.size() && IsJsSpace(name[end])) ++end;
            if (end == spacesStart)
            {
                return std::nullopt;
            }
            if (end < name.size())
            {
                return end;
            }
            // All spaces to the end: give one back to the rest if there is one
            // to spare, as the regular expression would by backtracking.
            return end - spacesStart >= 2 ? std::optional<std::size_t>{ end - 1 } : std::nullopt;
        };

        // `\s+[-\u2013\u2014|]\s+`
        if (at < name.size() && IsJsSpace(name[at]))
        {
            auto dash = at;
            while (dash < name.size() && IsJsSpace(name[dash])) ++dash;
            if (dash < name.size() && IsSeparatorDash(name[dash]))
            {
                if (auto const rest = restAfterSpaces(dash + 1))
                {
                    return rest;
                }
            }
        }
        // `:\s+`
        if (at < name.size() && name[at] == L':')
        {
            if (auto const rest = restAfterSpaces(at + 1))
            {
                return rest;
            }
        }
        return std::nullopt;
    }

    bool IsAcronym(std::wstring_view value) noexcept
    {
        if (value.size() < 2 || value.size() > 6)
        {
            return false;
        }
        return std::all_of(value.begin(), value.end(), [](wchar_t c)
        {
            return (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9');
        });
    }

    // `split(/\s+/)`: a run of spaces is one separator.
    std::vector<std::wstring> SplitOnSpaces(std::wstring_view value)
    {
        std::vector<std::wstring> words;
        std::wstring current;
        bool inSpaces{};
        for (auto const c : value)
        {
            if (IsJsSpace(c))
            {
                if (!inSpaces)
                {
                    words.push_back(std::move(current));
                    current.clear();
                }
                inSpaces = true;
                continue;
            }
            inSpaces = false;
            current.push_back(c);
        }
        words.push_back(std::move(current));
        return words;
    }

    struct SplitName final
    {
        std::wstring Rest;
        std::optional<std::wstring> Acronym;
    };

    // The addon's own prefix goes when it is an acronym or the addon's own name
    // or first word; anything else could be part of the title and stays.
    SplitName SplitAddonPrefix(std::wstring const& name, std::wstring_view addonName)
    {
        // `^(.+?)`: the shortest prefix after which a separator matches.
        for (std::size_t length = 1; length < name.size(); ++length)
        {
            auto const restStart = RestAfterSeparator(name, length);
            if (!restStart)
            {
                continue;
            }
            auto prefix = Trim(std::wstring_view{ name }.substr(0, length));
            auto rest = Trim(std::wstring_view{ name }.substr(*restStart));
            if (IsAcronym(prefix))
            {
                return { std::move(rest), std::move(prefix) };
            }
            auto const addon = Lower(Trim(WithoutAddonSuffix(addonName)));
            auto const lower = Lower(prefix);
            auto const firstWord = SplitOnSpaces(addon).front();
            if (lower == addon || lower == firstWord)
            {
                return { std::move(rest), std::nullopt };
            }
            return { name, std::nullopt };
        }
        return { name, std::nullopt };
    }
}

namespace HaloDesktop::Services
{
    bool IsHomeCatalogType(std::wstring_view type) noexcept
    {
        return type == L"movie" || type == L"series";
    }

    ShelfLabels CatalogShelfLabels(
        std::optional<std::wstring_view> catalogName,
        std::wstring_view type,
        std::wstring_view addonName)
    {
        auto const kind = LabelsForKind(type);
        auto split = SplitAddonPrefix(Trim(catalogName.value_or(std::wstring_view{})), addonName);
        auto source = Upper(split.Acronym ? *split.Acronym : Trim(WithoutAddonSuffix(addonName)));
        auto const& name = split.Rest;
        if (name.empty())
        {
            return { kind.Plural, std::move(source) };
        }

        bool saysKind{};
        if (type == L"movie")
        {
            saysKind = ContainsWholeWord(name, MovieKindWords);
        }
        else if (type == L"series")
        {
            saysKind = ContainsWholeWord(name, SeriesKindWords);
        }
        else
        {
            saysKind = Lower(name).find(Lower(type)) != std::wstring::npos;
        }
        if (saysKind)
        {
            return { name, std::move(source) };
        }

        auto words = SplitOnSpaces(name);
        auto const lastLower = Lower(words.back());
        auto const trailing = std::find(TrailingNouns.begin(), TrailingNouns.end(), lastLower) != TrailingNouns.end();
        if (words.size() > 1 && trailing)
        {
            auto const last = std::move(words.back());
            words.pop_back();
            words.push_back(kind.Singular);
            words.push_back(last);
            std::wstring title;
            for (auto const& word : words)
            {
                if (!title.empty()) title.push_back(L' ');
                title.append(word);
            }
            return { std::move(title), std::move(source) };
        }
        return { name + L" " + kind.Plural, std::move(source) };
    }

    bool MatchesHomeFilterType(::HaloDesktop::ViewModels::HomeFilter filter, std::wstring_view type) noexcept
    {
        return ::HaloDesktop::ViewModels::MatchesHomeFilter(
            filter,
            type == L"series"
                ? ::HaloDesktop::ViewModels::HomeMediaKind::Series
                : ::HaloDesktop::ViewModels::HomeMediaKind::Movie);
    }

    std::vector<RecentlyWatchedTitle> BuildRecentlyWatchedRow(
        std::vector<RecentlyWatchedRow> rows,
        std::span<RecentlyWatchedLibraryEntry const> library,
        std::unordered_set<std::wstring> const& exclude,
        ::HaloDesktop::ViewModels::HomeFilter filter,
        std::size_t limit)
    {
        std::unordered_map<std::wstring, RecentlyWatchedLibraryEntry const*> libraryById;
        for (auto const& entry : library)
        {
            if (!entry.Removed)
            {
                libraryById.insert_or_assign(entry.Id, &entry);
            }
        }

        // Stable, so rows stamped alike keep the order the server gave them.
        std::stable_sort(rows.begin(), rows.end(), [](RecentlyWatchedRow const& a, RecentlyWatchedRow const& b)
        {
            return a.UpdatedAt > b.UpdatedAt;
        });

        std::unordered_set<std::wstring> seen;
        std::vector<RecentlyWatchedTitle> shelf;
        for (auto& row : rows)
        {
            if (shelf.size() >= limit)
            {
                break;
            }
            if (exclude.contains(row.ItemId) || seen.contains(row.ItemId))
            {
                continue;
            }
            // Claimed before the name check, so an unnamed newest row still
            // speaks for its title and an older row of it is not shown instead.
            seen.insert(row.ItemId);
            auto const found = libraryById.find(row.ItemId);
            auto const* entry = found != libraryById.end() ? found->second : nullptr;
            // The row's own value wins whenever it has one, even an empty one.
            auto name = row.Name ? *row.Name : (entry ? entry->Name : std::wstring{});
            if (name.empty())
            {
                continue;
            }
            auto type = TypeFromItemId(row.ItemId);
            if (!MatchesHomeFilterType(filter, type))
            {
                continue;
            }
            auto poster = row.Poster ? row.Poster : (entry ? entry->Poster : std::nullopt);
            shelf.push_back({ std::move(type), MetaIdFromItemId(row.ItemId), std::move(name), std::move(poster) });
        }
        return shelf;
    }
}
