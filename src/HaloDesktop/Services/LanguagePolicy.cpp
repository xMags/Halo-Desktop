#include "Services/LanguagePolicy.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace
{
    struct CodeLabel final
    {
        std::wstring_view Code;
        std::wstring_view Label;
    };

    struct TwoLetterCode final
    {
        std::wstring_view TwoLetter;
        std::wstring_view ThreeLetter;
    };

    // Order matters: it is the order code variants are listed in, as in the
    // reference tables.
    constexpr auto LanguageLabels = std::to_array<CodeLabel>({
        { L"ara", L"Arabic" },
        { L"bul", L"Bulgarian" },
        { L"chi", L"Chinese" },
        { L"zho", L"Chinese" },
        { L"cze", L"Czech" },
        { L"ces", L"Czech" },
        { L"dan", L"Danish" },
        { L"dut", L"Dutch" },
        { L"nld", L"Dutch" },
        { L"eng", L"English" },
        { L"est", L"Estonian" },
        { L"fin", L"Finnish" },
        { L"fre", L"French" },
        { L"fra", L"French" },
        { L"ger", L"German" },
        { L"deu", L"German" },
        { L"gre", L"Greek" },
        { L"ell", L"Greek" },
        { L"heb", L"Hebrew" },
        { L"hin", L"Hindi" },
        { L"hrv", L"Croatian" },
        { L"hun", L"Hungarian" },
        { L"ind", L"Indonesian" },
        { L"ita", L"Italian" },
        { L"jpn", L"Japanese" },
        { L"kor", L"Korean" },
        { L"lav", L"Latvian" },
        { L"lit", L"Lithuanian" },
        { L"may", L"Malay" },
        { L"msa", L"Malay" },
        { L"nor", L"Norwegian" },
        { L"per", L"Persian" },
        { L"fas", L"Persian" },
        { L"pol", L"Polish" },
        { L"por", L"Portuguese" },
        { L"pob", L"Portuguese (BR)" },
        { L"rum", L"Romanian" },
        { L"ron", L"Romanian" },
        { L"rus", L"Russian" },
        { L"slo", L"Slovak" },
        { L"slk", L"Slovak" },
        { L"slv", L"Slovenian" },
        { L"spa", L"Spanish" },
        { L"srp", L"Serbian" },
        { L"swe", L"Swedish" },
        { L"tha", L"Thai" },
        { L"tur", L"Turkish" },
        { L"ukr", L"Ukrainian" },
        { L"vie", L"Vietnamese" },
    });

    constexpr auto TwoLetterCodes = std::to_array<TwoLetterCode>({
        { L"ar", L"ara" }, { L"bg", L"bul" }, { L"zh", L"chi" }, { L"cs", L"cze" }, { L"da", L"dan" },
        { L"nl", L"dut" }, { L"en", L"eng" }, { L"et", L"est" }, { L"fi", L"fin" }, { L"fr", L"fre" },
        { L"de", L"ger" }, { L"el", L"gre" }, { L"he", L"heb" }, { L"hi", L"hin" }, { L"hr", L"hrv" },
        { L"hu", L"hun" }, { L"id", L"ind" }, { L"it", L"ita" }, { L"ja", L"jpn" }, { L"ko", L"kor" },
        { L"lv", L"lav" }, { L"lt", L"lit" }, { L"ms", L"may" }, { L"no", L"nor" }, { L"nb", L"nor" },
        { L"fa", L"per" }, { L"pl", L"pol" }, { L"pt", L"por" }, { L"ro", L"rum" }, { L"ru", L"rus" },
        { L"sk", L"slo" }, { L"sl", L"slv" }, { L"es", L"spa" }, { L"sr", L"srp" }, { L"sv", L"swe" },
        { L"th", L"tha" }, { L"tr", L"tur" }, { L"uk", L"ukr" }, { L"vi", L"vie" },
    });

    constexpr auto Options = std::to_array<HaloDesktop::Services::LanguageOption>({
        { L"eng", L"English" }, { L"spa", L"Spanish" }, { L"por", L"Portuguese" },
        { L"pob", L"Portuguese (BR)" }, { L"fre", L"French" }, { L"ger", L"German" },
        { L"ita", L"Italian" }, { L"dut", L"Dutch" }, { L"pol", L"Polish" }, { L"rus", L"Russian" },
        { L"ukr", L"Ukrainian" }, { L"swe", L"Swedish" }, { L"nor", L"Norwegian" }, { L"dan", L"Danish" },
        { L"fin", L"Finnish" }, { L"cze", L"Czech" }, { L"slo", L"Slovak" }, { L"slv", L"Slovenian" },
        { L"hrv", L"Croatian" }, { L"srp", L"Serbian" }, { L"hun", L"Hungarian" }, { L"rum", L"Romanian" },
        { L"bul", L"Bulgarian" }, { L"gre", L"Greek" }, { L"tur", L"Turkish" }, { L"ara", L"Arabic" },
        { L"heb", L"Hebrew" }, { L"per", L"Persian" }, { L"hin", L"Hindi" }, { L"tha", L"Thai" },
        { L"vie", L"Vietnamese" }, { L"ind", L"Indonesian" }, { L"may", L"Malay" }, { L"chi", L"Chinese" },
        { L"jpn", L"Japanese" }, { L"kor", L"Korean" }, { L"est", L"Estonian" }, { L"lav", L"Latvian" },
        { L"lit", L"Lithuanian" },
    });

    constexpr std::wstring_view BrazilianPortugueseTag = L"pt-br";
    constexpr std::wstring_view BrazilianPortugueseCode = L"pob";

    std::optional<std::wstring_view> LabelFor(std::wstring_view code)
    {
        for (auto const& entry : LanguageLabels)
        {
            if (entry.Code == code) return entry.Label;
        }
        return std::nullopt;
    }

    std::optional<std::wstring_view> ThreeLetterFor(std::wstring_view twoLetter)
    {
        for (auto const& entry : TwoLetterCodes)
        {
            if (entry.TwoLetter == twoLetter) return entry.ThreeLetter;
        }
        return std::nullopt;
    }

    bool IsJavaScriptWhitespace(wchar_t value) noexcept
    {
        return value == L' ' || value == L'\t' || value == L'\n' || value == L'\r' || value == L'\v'
            || value == L'\f' || value == 0x00A0 || value == 0x1680 || (value >= 0x2000 && value <= 0x200A)
            || value == 0x2028 || value == 0x2029 || value == 0x202F || value == 0x205F
            || value == 0x3000 || value == 0xFEFF;
    }

    std::wstring_view Trim(std::wstring_view value) noexcept
    {
        while (!value.empty() && IsJavaScriptWhitespace(value.front())) value.remove_prefix(1);
        while (!value.empty() && IsJavaScriptWhitespace(value.back())) value.remove_suffix(1);
        return value;
    }

    std::wstring LowerAscii(std::wstring_view value)
    {
        std::wstring result{ value };
        for (auto& character : result)
        {
            if (character >= L'A' && character <= L'Z') character = static_cast<wchar_t>(character - L'A' + L'a');
        }
        return result;
    }

    // The tag as the tables compare it: trimmed, lower case, `_` read as `-`.
    std::wstring NormalizedTag(std::wstring_view tag)
    {
        auto result = LowerAscii(Trim(tag));
        std::replace(result.begin(), result.end(), L'_', L'-');
        return result;
    }

    // The label of a known language, keeping its case; empty when unknown.
    std::optional<std::wstring_view> KnownLabel(std::wstring const& normalized)
    {
        if (normalized == BrazilianPortugueseTag) return LabelFor(BrazilianPortugueseCode);
        auto const primary = std::wstring_view{ normalized }.substr(0, normalized.find(L'-'));
        auto const code = ThreeLetterFor(primary).value_or(primary);
        if (auto const exact = LabelFor(normalized)) return exact;
        return LabelFor(code);
    }

    // languageIdentity: the language a tag names as this table's label, or the
    // tag itself, lower case.
    std::wstring Identity(std::wstring_view tag)
    {
        auto const normalized = NormalizedTag(tag);
        if (auto const label = KnownLabel(normalized)) return LowerAscii(*label);
        return normalized;
    }
}

namespace HaloDesktop::Services
{
    std::wstring LanguageLabel(std::wstring_view code)
    {
        auto const normalized = LowerAscii(Trim(code));
        if (auto const label = LabelFor(normalized)) return std::wstring{ *label };
        return std::wstring{ code };
    }

    std::optional<std::wstring> LanguageNameForTag(std::wstring_view tag)
    {
        if (auto const label = KnownLabel(NormalizedTag(tag))) return std::wstring{ *label };
        return std::nullopt;
    }

    bool LanguageTagsMatch(std::wstring_view subtitleLanguage, std::wstring_view preferredCode)
    {
        return Identity(subtitleLanguage) == Identity(preferredCode);
    }

    std::vector<std::wstring> LanguageCodeVariants(std::wstring_view preferredCode)
    {
        auto const given = LowerAscii(Trim(preferredCode));
        if (given.empty()) return {};
        auto const identity = Identity(given);

        std::vector<std::wstring_view> threeLetter;
        for (auto const& entry : LanguageLabels)
        {
            if (LowerAscii(entry.Label) == identity) threeLetter.push_back(entry.Code);
        }

        std::vector<std::wstring> result;
        auto const add = [&result](std::wstring_view code)
        {
            if (std::find(result.begin(), result.end(), code) == result.end()) result.emplace_back(code);
        };
        add(given);
        for (auto const code : threeLetter) add(code);
        if (auto const brazilian = LabelFor(BrazilianPortugueseCode); brazilian && LowerAscii(*brazilian) == identity)
        {
            add(BrazilianPortugueseTag);
        }
        for (auto const& entry : TwoLetterCodes)
        {
            if (std::find(threeLetter.begin(), threeLetter.end(), entry.ThreeLetter) != threeLetter.end())
            {
                add(entry.TwoLetter);
            }
        }
        return result;
    }

    std::span<LanguageOption const> LanguageOptions() noexcept
    {
        return Options;
    }
}
