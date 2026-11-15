#include "Api/ActionEntryPolicy.h"

#include <array>
#include <cstddef>
#include <vector>

namespace
{
    constexpr char32_t Wastebasket = 0x1F5D1;
    constexpr std::size_t MaximumGapCodePoints = 40;

    constexpr std::array<std::u32string_view, 6> ActionVerbs{
        U"delete", U"remove", U"clear", U"wipe", U"erase", U"purge",
    };

    // `files?`, `downloads?` and `torrents?` are spelled out both ways, because
    // each alternative must also end on a word boundary.
    constexpr std::array<std::u32string_view, 11> ActionTargets{
        U"all", U"file", U"files", U"everything", U"watchlist", U"library",
        U"download", U"downloads", U"torrent", U"torrents", U"cache",
    };

    constexpr std::array<std::wstring_view, 6> UrlVerbs{
        L"delete", L"remove", L"clear", L"wipe", L"erase", L"purge",
    };

    constexpr std::array<std::wstring_view, 4> UrlTargets{
        L"all", L"watchlist", L"files", L"everything",
    };

    // UTF-16 to code points. A lone surrogate stays as its own unit, the way a
    // JavaScript `u` regular expression sees it.
    std::vector<char32_t> CodePoints(std::wstring_view text)
    {
        std::vector<char32_t> result;
        result.reserve(text.size());
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            auto const unit = static_cast<char32_t>(text[index]);
            if (unit >= 0xD800 && unit <= 0xDBFF && index + 1 < text.size())
            {
                auto const next = static_cast<char32_t>(text[index + 1]);
                if (next >= 0xDC00 && next <= 0xDFFF)
                {
                    result.push_back(0x10000 + ((unit - 0xD800) << 10) + (next - 0xDC00));
                    ++index;
                    continue;
                }
            }
            result.push_back(unit);
        }
        return result;
    }

    // Unicode simple case folding as far as it reaches the ASCII letters the
    // patterns use: ASCII upper case, LONG S (to s) and KELVIN SIGN (to k).
    char32_t FoldForText(char32_t value) noexcept
    {
        if (value >= U'A' && value <= U'Z') return value - U'A' + U'a';
        if (value == 0x017F) return U's';
        if (value == 0x212A) return U'k';
        return value;
    }

    // `\w` under the `iu` flags: ASCII word characters plus the two code
    // points whose case folding lands on one.
    bool IsTextWordCharacter(char32_t value) noexcept
    {
        return (value >= U'a' && value <= U'z') || (value >= U'A' && value <= U'Z')
            || (value >= U'0' && value <= U'9') || value == U'_'
            || value == 0x017F || value == 0x212A;
    }

    bool IsLineTerminator(char32_t value) noexcept
    {
        return value == U'\n' || value == U'\r' || value == 0x2028 || value == 0x2029;
    }

    bool IsBoundary(std::vector<char32_t> const& text, std::size_t position) noexcept
    {
        auto const before = position > 0 && IsTextWordCharacter(text[position - 1]);
        auto const after = position < text.size() && IsTextWordCharacter(text[position]);
        return before != after;
    }

    bool WordAt(std::vector<char32_t> const& text, std::size_t position, std::u32string_view word) noexcept
    {
        if (position + word.size() > text.size()) return false;
        for (std::size_t offset = 0; offset < word.size(); ++offset)
        {
            if (FoldForText(text[position + offset]) != word[offset]) return false;
        }
        return IsBoundary(text, position) && IsBoundary(text, position + word.size());
    }

    bool TargetAt(std::vector<char32_t> const& text, std::size_t position) noexcept
    {
        for (auto const target : ActionTargets)
        {
            if (WordAt(text, position, target)) return true;
        }
        return false;
    }

    wchar_t FoldAscii(wchar_t value) noexcept
    {
        return value >= L'A' && value <= L'Z' ? static_cast<wchar_t>(value - L'A' + L'a') : value;
    }

    bool AsciiWordAt(std::wstring_view text, std::size_t position, std::wstring_view word) noexcept
    {
        if (position + word.size() > text.size()) return false;
        for (std::size_t offset = 0; offset < word.size(); ++offset)
        {
            if (FoldAscii(text[position + offset]) != word[offset]) return false;
        }
        return true;
    }

    bool UrlTargetAt(std::wstring_view text, std::size_t position) noexcept
    {
        for (auto const target : UrlTargets)
        {
            if (AsciiWordAt(text, position, target)) return true;
        }
        return false;
    }
}

namespace HaloDesktop::Api
{
    // /^dl[a-z]+$/
    bool MatchesActionId(std::wstring_view id) noexcept
    {
        if (id.size() < 3 || id[0] != L'd' || id[1] != L'l') return false;
        for (auto const character : id.substr(2))
        {
            if (character < L'a' || character > L'z') return false;
        }
        return true;
    }

    // /\u{1F5D1}|\b(delete|remove|clear|wipe|erase|purge)\b.{0,40}?\b(all|files?|everything|
    //  watchlist|library|downloads?|torrents?|cache)\b/iu
    bool MatchesActionText(std::wstring_view text)
    {
        if (text.empty()) return false;
        auto const points = CodePoints(text);
        for (auto const point : points)
        {
            if (point == Wastebasket) return true;
        }

        for (std::size_t start = 0; start < points.size(); ++start)
        {
            for (auto const verb : ActionVerbs)
            {
                if (!WordAt(points, start, verb)) continue;
                auto const gapStart = start + verb.size();
                for (std::size_t gap = 0; gap <= MaximumGapCodePoints; ++gap)
                {
                    auto const targetStart = gapStart + gap;
                    if (targetStart > points.size()) break;
                    // `.` crosses anything but a line terminator.
                    if (gap > 0 && IsLineTerminator(points[targetStart - 1])) break;
                    if (TargetAt(points, targetStart)) return true;
                }
            }
        }
        return false;
    }

    // /(delete|remove|clear|wipe|erase|purge)[_-]?(all|watchlist|files|everything)/i
    bool MatchesActionUrl(std::wstring_view url) noexcept
    {
        for (std::size_t start = 0; start < url.size(); ++start)
        {
            for (auto const verb : UrlVerbs)
            {
                if (!AsciiWordAt(url, start, verb)) continue;
                auto const next = start + verb.size();
                if (UrlTargetAt(url, next)) return true;
                if (next < url.size() && (url[next] == L'_' || url[next] == L'-') && UrlTargetAt(url, next + 1))
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool IsActionEntry(std::wstring_view id, std::wstring_view name)
    {
        return MatchesActionId(id) || MatchesActionText(name);
    }

    bool IsActionStream(ActionStreamFields const& stream)
    {
        if (!stream.Url.empty() && MatchesActionUrl(stream.Url)) return true;
        return MatchesActionText(stream.Name)
            || MatchesActionText(stream.Title)
            || MatchesActionText(stream.Description);
    }
}
