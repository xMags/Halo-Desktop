#include "Services/CatalogLanguagePolicy.h"

#include "Services/LanguagePolicy.h"

#include <algorithm>
#include <unordered_set>

namespace
{
    // Language labels are ASCII, so folding ASCII is enough to compare them.
    std::wstring FoldAscii(std::wstring_view value)
    {
        std::wstring result{ value };
        std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c)
        {
            return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c;
        });
        return result;
    }

    std::wstring_view TrimSpaces(std::wstring_view value) noexcept
    {
        auto const isSpace = [](wchar_t c)
        {
            return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\v' || c == L'\f'
                || c == 0x00A0 || c == 0xFEFF;
        };
        while (!value.empty() && isSpace(value.front())) value.remove_prefix(1);
        while (!value.empty() && isSpace(value.back())) value.remove_suffix(1);
        return value;
    }

    std::unordered_set<std::wstring> const& KnownLanguages()
    {
        static std::unordered_set<std::wstring> const known = []
        {
            std::unordered_set<std::wstring> labels;
            for (auto const& option : ::HaloDesktop::Services::LanguageOptions())
            {
                labels.insert(FoldAscii(option.Label));
            }
            return labels;
        }();
        return known;
    }
}

namespace HaloDesktop::Services
{
    std::optional<std::vector<std::wstring>> LanguageChoices(std::span<CatalogExtraDeclaration const> extras)
    {
        auto const genre = std::find_if(extras.begin(), extras.end(), [](CatalogExtraDeclaration const& extra)
        {
            return extra.Name == L"genre";
        });
        if (genre == extras.end() || genre->IsRequired)
        {
            return std::nullopt;
        }
        auto const& known = KnownLanguages();
        auto const count = std::count_if(genre->Options.begin(), genre->Options.end(), [&known](std::wstring const& option)
        {
            return known.contains(FoldAscii(TrimSpaces(option)));
        });
        if (static_cast<std::size_t>(count) < MinimumKnownLanguageOptions)
        {
            return std::nullopt;
        }
        return genre->Options;
    }

    std::optional<std::wstring> CatalogLanguageGenre(
        std::span<CatalogExtraDeclaration const> extras,
        std::optional<std::wstring_view> preferred)
    {
        if (!preferred || preferred->empty())
        {
            return std::nullopt;
        }
        auto const choices = LanguageChoices(extras);
        if (!choices || std::find(choices->begin(), choices->end(), *preferred) == choices->end())
        {
            return std::nullopt;
        }
        return std::wstring{ *preferred };
    }
}
