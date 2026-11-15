#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::Services
{
    // The reference client's `home/catalogLanguage.ts`: which catalogs are
    // language rows (TMDB's "Language" catalog), and the extra Home asks them
    // with for the account's chosen language.

    // One `extra` declaration of a manifest catalog.
    struct CatalogExtraDeclaration final
    {
        std::wstring Name;
        bool IsRequired{};
        std::vector<std::wstring> Options;
    };

    // How many known language names an option list must hold to count as a
    // language choice rather than a genre or a year list.
    inline constexpr std::size_t MinimumKnownLanguageOptions = 5;

    // The languages a catalog can be asked for when it is a language catalog:
    // an optional `genre` extra whose options include at least five language
    // names Halo knows. Empty for every other catalog.
    [[nodiscard]] std::optional<std::vector<std::wstring>> LanguageChoices(
        std::span<CatalogExtraDeclaration const> extras);

    // The `genre` value Home sends: the preferred language, for a language
    // catalog that offers it exactly; nothing otherwise, so the catalog falls
    // back to its own default.
    [[nodiscard]] std::optional<std::wstring> CatalogLanguageGenre(
        std::span<CatalogExtraDeclaration const> extras,
        std::optional<std::wstring_view> preferred);
}
