#include "Services/SearchResultsPolicy.h"

#include <windows.h>

#include <algorithm>
#include <climits>
#include <unordered_set>
#include <utility>
#include <winrt/Windows.Data.Text.h>

// NormalizeString lives in Normaliz.dll, present on every Windows this app
// supports. Named here so the app and the test projects that compile this
// file by path both link it without a project setting of their own.
#pragma comment(lib, "Normaliz.lib")

namespace
{
    using HaloDesktop::Services::SearchTitle;
    using winrt::Windows::Data::Text::UnicodeCharacters;
    using winrt::Windows::Data::Text::UnicodeGeneralCategory;

    constexpr int ExactName = 0;
    constexpr int BeginsWithQuery = 2;
    constexpr int SomeWords = 5;
    constexpr int UnrelatedName = 6;

    bool IsAscii(std::wstring_view text) noexcept
    {
        return std::all_of(text.begin(), text.end(), [](wchar_t unit) { return unit < 0x80; });
    }

    bool IsAsciiDigit(wchar_t unit) noexcept { return unit >= L'0' && unit <= L'9'; }
    bool IsAsciiUpper(wchar_t unit) noexcept { return unit >= L'A' && unit <= L'Z'; }
    bool IsAsciiLower(wchar_t unit) noexcept { return unit >= L'a' && unit <= L'z'; }
    bool IsAsciiAlpha(wchar_t unit) noexcept { return IsAsciiUpper(unit) || IsAsciiLower(unit); }
    bool IsAsciiAlnum(wchar_t unit) noexcept { return IsAsciiAlpha(unit) || IsAsciiDigit(unit); }

    wchar_t AsciiLower(wchar_t unit) noexcept
    {
        return IsAsciiUpper(unit) ? static_cast<wchar_t>(unit - L'A' + L'a') : unit;
    }

    bool EqualsAsciiNoCase(std::wstring_view text, std::wstring_view lowerLiteral) noexcept
    {
        if (text.size() != lowerLiteral.size()) return false;
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            if (AsciiLower(text[index]) != lowerLiteral[index]) return false;
        }
        return true;
    }

    // JavaScript's `\s` and the set `trim` strips.
    bool IsJsWhitespace(char32_t point) noexcept
    {
        return (point >= 0x09 && point <= 0x0D) || point == 0x20 || point == 0xA0 || point == 0x1680
            || (point >= 0x2000 && point <= 0x200A) || point == 0x2028 || point == 0x2029 || point == 0x202F
            || point == 0x205F || point == 0x3000 || point == 0xFEFF;
    }

    // One code point from `text` at `index`, advancing it. A lone surrogate is
    // read as itself, as JavaScript's code point iteration does.
    char32_t NextCodePoint(std::wstring_view text, std::size_t& index) noexcept
    {
        auto const unit = static_cast<char32_t>(text[index++]);
        if (unit >= 0xD800 && unit <= 0xDBFF && index < text.size())
        {
            auto const low = static_cast<char32_t>(text[index]);
            if (low >= 0xDC00 && low <= 0xDFFF)
            {
                ++index;
                return 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
            }
        }
        return unit;
    }

    UnicodeGeneralCategory CategoryOf(char32_t point)
    {
        return UnicodeCharacters::GetGeneralCategory(static_cast<std::uint32_t>(point));
    }

    // `\p{M}`.
    bool IsMark(char32_t point)
    {
        if (point < 0x80) return false;
        auto const category = CategoryOf(point);
        return category == UnicodeGeneralCategory::NonspacingMark
            || category == UnicodeGeneralCategory::SpacingCombiningMark
            || category == UnicodeGeneralCategory::EnclosingMark;
    }

    // `[\p{L}\p{N}]`.
    bool IsLetterOrNumber(char32_t point)
    {
        if (point < 0x80) return IsAsciiAlnum(static_cast<wchar_t>(point));
        switch (CategoryOf(point))
        {
        case UnicodeGeneralCategory::UppercaseLetter:
        case UnicodeGeneralCategory::LowercaseLetter:
        case UnicodeGeneralCategory::TitlecaseLetter:
        case UnicodeGeneralCategory::ModifierLetter:
        case UnicodeGeneralCategory::OtherLetter:
        case UnicodeGeneralCategory::DecimalDigitNumber:
        case UnicodeGeneralCategory::LetterNumber:
        case UnicodeGeneralCategory::OtherNumber:
            return true;
        default:
            return false;
        }
    }

    // Canonical decomposition. Text Windows cannot normalise (an unpaired
    // surrogate) is folded as it stands rather than dropped.
    std::wstring Decomposed(std::wstring_view text)
    {
        if (text.empty() || IsAscii(text) || text.size() > static_cast<std::size_t>(INT_MAX / 4))
        {
            return std::wstring{ text };
        }
        auto const length = static_cast<int>(text.size());
        auto estimate = NormalizeString(NormalizationD, text.data(), length, nullptr, 0);
        for (int attempt = 0; attempt < 8 && estimate > 0; ++attempt)
        {
            std::wstring buffer(static_cast<std::size_t>(estimate), L'\0');
            auto const written = NormalizeString(NormalizationD, text.data(), length, buffer.data(), estimate);
            if (written > 0)
            {
                buffer.resize(static_cast<std::size_t>(written));
                return buffer;
            }
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            {
                break;
            }
            // A negative result is the function's next guess at the size.
            estimate = written < 0 ? -written : estimate * 2;
        }
        return std::wstring{ text };
    }

    std::vector<std::wstring> SplitOnNonLettersOrNumbers(std::wstring_view text)
    {
        std::vector<std::wstring> words;
        std::wstring current;
        std::size_t index = 0;
        while (index < text.size())
        {
            auto const start = index;
            auto const point = NextCodePoint(text, index);
            if (IsLetterOrNumber(point))
            {
                current.append(text.substr(start, index - start));
                continue;
            }
            if (!current.empty())
            {
                words.push_back(std::move(current));
                current.clear();
            }
        }
        if (!current.empty())
        {
            words.push_back(std::move(current));
        }
        return words;
    }

    std::vector<std::wstring> SplitOnWhitespace(std::wstring_view text)
    {
        std::vector<std::wstring> words;
        std::wstring current;
        std::size_t index = 0;
        while (index < text.size())
        {
            auto const start = index;
            auto const point = NextCodePoint(text, index);
            if (!IsJsWhitespace(point))
            {
                current.append(text.substr(start, index - start));
                continue;
            }
            if (!current.empty())
            {
                words.push_back(std::move(current));
                current.clear();
            }
        }
        if (!current.empty())
        {
            words.push_back(std::move(current));
        }
        return words;
    }

    // A name or query as plain words: folded, punctuation as spaces, short
    // forms spelled out ("Dr. Strange" is "Doctor Strange").
    std::vector<std::wstring> PlainWords(std::wstring_view text)
    {
        auto words = SplitOnNonLettersOrNumbers(HaloDesktop::Services::FoldedText(text));
        for (auto& word : words)
        {
            if (word == L"dr") word = L"doctor";
            else if (word == L"mr") word = L"mister";
            else if (word == L"vs") word = L"versus";
        }
        return words;
    }

    std::wstring Joined(std::vector<std::wstring> const& words)
    {
        std::wstring joined;
        for (auto const& word : words)
        {
            if (!joined.empty()) joined.push_back(L' ');
            joined.append(word);
        }
        return joined;
    }

    bool Truthy(std::optional<std::wstring> const& value) noexcept
    {
        return value.has_value() && !value->empty();
    }

    // MediaFusion's id for an entry it made from a file name: `^mf\d+$`.
    bool IsFileNameEntry(std::wstring_view id) noexcept
    {
        return id.size() > 2 && id.starts_with(L"mf")
            && std::all_of(id.begin() + 2, id.end(), [](wchar_t unit) { return IsAsciiDigit(unit); });
    }

    // The TMDB addon's link built from a missing image: `/\/t\/p\/[a-z0-9]+null$/i`.
    bool IsTmdbMissingImage(std::wstring_view url) noexcept
    {
        if (url.size() < 4 || !EqualsAsciiNoCase(url.substr(url.size() - 4), L"null")) return false;
        auto const end = url.size() - 4;
        auto start = end;
        while (start > 0 && IsAsciiAlnum(url[start - 1])) --start;
        // The size part follows a slash, so it can only be the whole run of
        // letters and digits before the final "null", and it is never empty.
        if (start == end) return false;
        return start >= 5 && EqualsAsciiNoCase(url.substr(start - 5, 5), L"/t/p/");
    }

    // `\/poster\/[a-z]+\/mf\d+\.[a-z]+` ending at `end`.
    bool IsGeneratedPosterEndingAt(std::wstring_view url, std::size_t end) noexcept
    {
        auto index = end;
        auto const extensionEnd = index;
        while (index > 0 && IsAsciiAlpha(url[index - 1])) --index;
        if (index == extensionEnd || index == 0 || url[index - 1] != L'.') return false;
        --index;
        auto const digitsEnd = index;
        while (index > 0 && IsAsciiDigit(url[index - 1])) --index;
        if (index == digitsEnd || index < 2 || !EqualsAsciiNoCase(url.substr(index - 2, 2), L"mf")) return false;
        index -= 2;
        if (index == 0 || url[index - 1] != L'/') return false;
        --index;
        auto const kindEnd = index;
        while (index > 0 && IsAsciiAlpha(url[index - 1])) --index;
        if (index == kindEnd || index < 8) return false;
        return EqualsAsciiNoCase(url.substr(index - 8, 8), L"/poster/");
    }

    // MediaFusion's drawn card for one of its own entries:
    // `/\/poster\/[a-z]+\/mf\d+\.[a-z]+(\?.*)?$/i`.
    bool IsGeneratedPoster(std::wstring_view url) noexcept
    {
        if (IsGeneratedPosterEndingAt(url, url.size())) return true;
        for (std::size_t index = 0; index < url.size(); ++index)
        {
            if (url[index] == L'?' && IsGeneratedPosterEndingAt(url, index)) return true;
        }
        return false;
    }

    // Stremio's own poster hosts: `^https:\/\/[a-z0-9-]+\.metahub\.space\/`.
    bool IsMetahubPoster(std::optional<std::wstring> const& poster) noexcept
    {
        if (!poster) return false;
        std::wstring_view const url{ *poster };
        constexpr std::wstring_view scheme{ L"https://" };
        if (!url.starts_with(scheme)) return false;
        auto index = scheme.size();
        while (index < url.size() && (IsAsciiLower(url[index]) || IsAsciiDigit(url[index]) || url[index] == L'-')) ++index;
        return index > scheme.size() && url.substr(index).starts_with(L".metahub.space/");
    }
}

namespace HaloDesktop::Services
{
    std::wstring LowerText(std::wstring_view text)
    {
        if (IsAscii(text))
        {
            std::wstring result{ text };
            std::transform(result.begin(), result.end(), result.begin(), [](wchar_t unit) { return AsciiLower(unit); });
            return result;
        }
        if (text.size() > static_cast<std::size_t>(INT_MAX / 4)) return std::wstring{ text };
        auto const length = static_cast<int>(text.size());
        auto const size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), length, nullptr, 0, nullptr, nullptr, 0);
        if (size <= 0) return std::wstring{ text };
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        auto const written = LCMapStringEx(
            LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), length, result.data(), size, nullptr, nullptr, 0);
        if (written <= 0) return std::wstring{ text };
        result.resize(static_cast<std::size_t>(written));
        return result;
    }

    std::wstring FoldedText(std::wstring_view text)
    {
        if (IsAscii(text)) return LowerText(text);
        auto const decomposed = Decomposed(text);
        std::wstring stripped;
        stripped.reserve(decomposed.size());
        std::wstring_view const view{ decomposed };
        std::size_t index = 0;
        while (index < view.size())
        {
            auto const start = index;
            auto const point = NextCodePoint(view, index);
            if (!IsMark(point))
            {
                stripped.append(view.substr(start, index - start));
            }
        }
        return LowerText(stripped);
    }

    std::wstring NameKey(std::wstring_view text)
    {
        return Joined(SplitOnNonLettersOrNumbers(FoldedText(text)));
    }

    std::wstring TrimmedText(std::wstring_view text)
    {
        while (!text.empty() && IsJsWhitespace(static_cast<char32_t>(text.front()))) text.remove_prefix(1);
        while (!text.empty() && IsJsWhitespace(static_cast<char32_t>(text.back()))) text.remove_suffix(1);
        return std::wstring{ text };
    }

    std::vector<std::wstring> WhitespaceWords(std::wstring_view text)
    {
        return SplitOnWhitespace(text);
    }

    std::wstring CappedQuery(std::wstring_view text)
    {
        if (text.size() <= SearchMaximumQuery) return std::wstring{ text };
        auto length = SearchMaximumQuery;
        auto const last = text[length - 1];
        if (last >= 0xD800 && last <= 0xDBFF) --length;
        return std::wstring{ text.substr(0, length) };
    }

    bool IsSearchableQuery(std::wstring_view query)
    {
        return TrimmedText(query).size() >= SearchMinimumQuery;
    }

    std::wstring TitleKey(SearchTitle const& title)
    {
        return title.Type + L':' + title.Id;
    }

    std::vector<SearchTitle> UniqueTitles(std::vector<SearchTitle> titles)
    {
        std::unordered_set<std::wstring> seen;
        std::vector<SearchTitle> unique;
        unique.reserve(titles.size());
        for (auto& title : titles)
        {
            if (seen.insert(TitleKey(title)).second) unique.push_back(std::move(title));
        }
        return unique;
    }

    int NameRelevance(std::wstring_view name, std::wstring_view query)
    {
        auto const wanted = PlainWords(query);
        if (wanted.empty()) return ExactName;
        auto const words = PlainWords(name);
        auto const joined = Joined(words);
        auto const phrase = Joined(wanted);
        if (joined == phrase) return ExactName;
        if (joined.starts_with(phrase + L' ')) return 1;
        if (joined.starts_with(phrase)) return BeginsWithQuery;
        auto const inWords = [&words](std::wstring const& word)
        {
            return std::find(words.begin(), words.end(), word) != words.end();
        };
        auto const inJoined = [&joined](std::wstring const& word)
        {
            return joined.find(word) != std::wstring::npos;
        };
        if (std::all_of(wanted.begin(), wanted.end(), inWords)) return 3;
        if (std::all_of(wanted.begin(), wanted.end(), inJoined)) return 4;
        if (std::any_of(wanted.begin(), wanted.end(), inJoined)) return SomeWords;
        return UnrelatedName;
    }

    std::vector<SearchTitle> ByRelevance(std::vector<SearchTitle> titles, std::wstring_view query)
    {
        std::vector<std::pair<int, std::size_t>> ranked;
        ranked.reserve(titles.size());
        for (std::size_t index = 0; index < titles.size(); ++index)
        {
            ranked.emplace_back(NameRelevance(titles[index].Name, query), index);
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        std::vector<SearchTitle> result;
        result.reserve(titles.size());
        for (auto const& [relevance, index] : ranked)
        {
            result.push_back(std::move(titles[index]));
        }
        return result;
    }

    std::optional<std::wstring> ReleaseYear(std::optional<std::wstring> const& releaseInfo)
    {
        if (!releaseInfo) return std::nullopt;
        std::wstring_view const text{ *releaseInfo };
        std::size_t run = 0;
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            run = IsAsciiDigit(text[index]) ? run + 1 : 0;
            if (run == 4) return std::wstring{ text.substr(index - 3, 4) };
        }
        return std::nullopt;
    }

    std::vector<std::wstring> TitleIdentities(SearchTitle const& title)
    {
        std::vector<std::wstring> identities{ TitleKey(title) };
        // The surest one when it is there: Cinemeta's id is the IMDb id, and
        // the TMDB addon sends it beside its own.
        std::optional<std::wstring> imdbId;
        if (IsImdbId(title.Id)) imdbId = title.Id;
        else if (title.ImdbId && IsImdbId(*title.ImdbId)) imdbId = title.ImdbId;
        if (imdbId) identities.push_back(title.Type + L"|imdb|" + *imdbId);
        auto const year = ReleaseYear(title.ReleaseInfo);
        std::wstring name;
        for (auto const& word : SplitOnNonLettersOrNumbers(FoldedText(title.Name)))
        {
            name.append(word);
        }
        // Without a year a name alone is not trusted, since remakes share names.
        if (year && !name.empty()) identities.push_back(title.Type + L'|' + name + L'|' + *year);
        return identities;
    }

    bool HasNoPoster(std::optional<std::wstring> const& poster, std::optional<std::wstring_view> id)
    {
        if (id && IsFileNameEntry(*id)) return true;
        return !Truthy(poster) || IsTmdbMissingImage(*poster) || IsGeneratedPoster(*poster);
    }

    bool HasNoPoster(SearchTitle const& title)
    {
        return HasNoPoster(title.Poster, std::wstring_view{ title.Id });
    }

    bool IsImdbId(std::wstring_view id) noexcept
    {
        return id.size() > 2 && id.starts_with(L"tt")
            && std::all_of(id.begin() + 2, id.end(), [](wchar_t unit) { return IsAsciiDigit(unit); });
    }

    bool IsProductCodeName(std::wstring_view name) noexcept
    {
        std::size_t letters = 0;
        while (letters < name.size() && IsAsciiUpper(name[letters])) ++letters;
        if (letters < 2 || letters > 6 || letters >= name.size() || name[letters] != L'-') return false;
        auto const digitsStart = letters + 1;
        auto index = digitsStart;
        while (index < name.size() && IsAsciiDigit(name[index])) ++index;
        auto const digits = index - digitsStart;
        if (digits < 3 || digits > 5) return false;
        if (index == name.size()) return true;
        auto const next = name[index];
        return !(IsAsciiAlnum(next) || next == L'_');
    }

    SearchTitle WithYearOutOfName(SearchTitle title)
    {
        std::wstring_view const name{ title.Name };
        if (name.size() < 7 || name.back() != L')' || name[name.size() - 6] != L'(') return title;
        auto const year = name.substr(name.size() - 5, 4);
        if (!std::all_of(year.begin(), year.end(), [](wchar_t unit) { return IsAsciiDigit(unit); })) return title;
        auto prefix = name.substr(0, name.size() - 6);
        while (!prefix.empty() && IsJsWhitespace(static_cast<char32_t>(prefix.back()))) prefix.remove_suffix(1);
        if (prefix.empty()) return title;
        std::wstring kept{ prefix };
        std::wstring yearText{ year };
        title.Name = std::move(kept);
        if (!ReleaseYear(title.ReleaseInfo))
        {
            title.ReleaseInfo = std::move(yearText);
        }
        return title;
    }

    SearchTitle FillMissingDetails(SearchTitle kept, SearchTitle const& other)
    {
        // A copy's poster known to be missing is no poster at all.
        auto const otherHasNone = HasNoPoster(other) || other.PosterMissing;
        auto const poster = HasNoPoster(kept) || IsMetahubPoster(kept.Poster) || kept.PosterMissing
            ? (otherHasNone ? kept.Poster : other.Poster)
            : kept.Poster;
        if (Truthy(poster)) kept.Poster = poster;
        if (!otherHasNone) kept.PosterMissing = false;
        if (!Truthy(kept.Rating) && Truthy(other.Rating)) kept.Rating = other.Rating;
        if (!Truthy(kept.ReleaseInfo) && Truthy(other.ReleaseInfo)) kept.ReleaseInfo = other.ReleaseInfo;
        if (!Truthy(kept.Description) && Truthy(other.Description)) kept.Description = other.Description;
        return kept;
    }

    SearchTitle MergeCopies(SearchTitle kept, SearchTitle const& other)
    {
        if (IsImdbId(kept.Id) || !IsImdbId(other.Id))
        {
            return FillMissingDetails(std::move(kept), other);
        }
        return FillMissingDetails(other, kept);
    }

    bool IsSearchedCatalogType(std::wstring_view type) noexcept
    {
        return type == L"movie" || type == L"series";
    }

    SearchProgressResult SearchProgress(
        std::span<SearchTarget const> targets,
        std::span<SearchTargetState const> states)
    {
        SearchProgressResult result;
        result.Total = targets.size();
        static SearchTargetState const pendingState{};
        for (std::size_t index = 0; index < targets.size(); ++index)
        {
            auto const& target = targets[index];
            auto const& state = index < states.size() ? states[index] : pendingState;
            if (state.Status == SearchTargetStatus::Pending)
            {
                ++result.Pending;
                continue;
            }
            if (state.Status == SearchTargetStatus::Failed)
            {
                result.Failed.push_back(target);
                continue;
            }
            if (state.Titles.empty()) continue;
            result.Groups.push_back({ target.Key, target.Title, target.AddonName, target.Type, state.Titles });
        }
        return result;
    }

    std::vector<SearchTitle> LibraryMatches(
        std::span<SearchLibraryEntry const> library,
        std::wstring_view query)
    {
        auto const words = SplitOnWhitespace(FoldedText(query));
        if (words.empty()) return {};
        std::vector<SearchLibraryEntry const*> matches;
        for (auto const& item : library)
        {
            if (item.Removed) continue;
            auto const name = FoldedText(item.Name);
            if (std::all_of(words.begin(), words.end(), [&name](std::wstring const& word)
                {
                    return name.find(word) != std::wstring::npos;
                }))
            {
                matches.push_back(&item);
            }
        }
        std::stable_sort(matches.begin(), matches.end(), [](SearchLibraryEntry const* a, SearchLibraryEntry const* b)
        {
            return a->AddedAt > b->AddedAt;
        });
        std::vector<SearchTitle> result;
        result.reserve(matches.size());
        for (auto const* item : matches)
        {
            SearchTitle title;
            auto const prefix = item->Type.size() + 1;
            title.Id = item->Id.size() >= prefix ? item->Id.substr(prefix) : std::wstring{};
            title.Type = item->Type;
            title.Name = item->Name;
            if (Truthy(item->Poster)) title.Poster = item->Poster;
            result.push_back(std::move(title));
        }
        return result;
    }

    std::vector<std::wstring> SuggestedSearches(
        std::span<std::wstring const> history,
        std::wstring_view typed,
        std::size_t limit)
    {
        auto const needle = FoldedText(TrimmedText(typed));
        if (needle.empty()) return {};
        std::vector<std::wstring> suggestions;
        for (auto const& term : history)
        {
            if (suggestions.size() == limit) break;
            auto const candidate = FoldedText(term);
            if (candidate != needle && candidate.find(needle) != std::wstring::npos)
            {
                suggestions.push_back(term);
            }
        }
        return suggestions;
    }

    std::vector<std::wstring> SearchSuggestions(
        std::span<std::wstring const> history,
        std::wstring_view typed,
        std::span<std::wstring const> found,
        std::size_t limit)
    {
        auto suggestions = SuggestedSearches(history, typed, limit);
        auto const needle = FoldedText(TrimmedText(typed));
        if (needle.empty()) return suggestions;
        std::vector<std::wstring> offered;
        for (auto const& suggestion : suggestions) offered.push_back(FoldedText(suggestion));
        for (auto const& name : found)
        {
            if (suggestions.size() >= limit) break;
            auto key = FoldedText(TrimmedText(name));
            if (key.empty() || key == needle || std::find(offered.begin(), offered.end(), key) != offered.end()) continue;
            offered.push_back(std::move(key));
            suggestions.push_back(TrimmedText(name));
        }
        return suggestions;
    }

    std::wstring SearchFailureNote(std::span<SearchTarget const> failed, std::size_t total)
    {
        if (failed.empty()) return {};
        if (failed.size() == total)
        {
            return L"Search could not reach your addons. Check your connection and try again.";
        }
        std::vector<std::wstring> names;
        for (auto const& target : failed)
        {
            if (std::find(names.begin(), names.end(), target.AddonName) == names.end()) names.push_back(target.AddonName);
        }
        std::wstring note;
        for (auto const& name : names)
        {
            if (!note.empty()) note.append(L", ");
            note.append(name);
        }
        note.append(names.size() == 1 ? L" did not answer, so its results are missing." : L" did not answer, so their results are missing.");
        return note;
    }

    std::wstring SearchStillAskingNote(std::size_t pending)
    {
        return L"Still asking " + std::to_wstring(pending) + (pending == 1 ? L" more catalog…" : L" more catalogs…");
    }
}
