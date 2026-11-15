#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::Services
{
    // The language tables and matching rules of the reference client's
    // `subtitles/languages.ts`: labels for the ISO 639-2 codes subtitle addons
    // emit, the two-letter codes files tag tracks with, and the identity two
    // tags are compared by. Tags are ASCII in practice, so case is folded for
    // ASCII letters only.

    struct LanguageOption final
    {
        std::wstring_view Code;
        std::wstring_view Label;
    };

    // The label for an exact three-letter code, else the code as given.
    [[nodiscard]] std::wstring LanguageLabel(std::wstring_view code);

    // The label of the language a tag names, however it is spelled (three or
    // two letters, a region or script suffix, `pt-BR`); empty when the tag is
    // not a language these tables know.
    [[nodiscard]] std::optional<std::wstring> LanguageNameForTag(std::wstring_view tag);

    // Whether a track's or subtitle's language tag names the preferred
    // language, however either side spells it.
    [[nodiscard]] bool LanguageTagsMatch(std::wstring_view subtitleLanguage, std::wstring_view preferredCode);

    // Every code that names the same language as the preference, the given one
    // first: what a player's own language preference is given so it picks the
    // track however the file tagged it. Empty for an empty preference.
    [[nodiscard]] std::vector<std::wstring> LanguageCodeVariants(std::wstring_view preferredCode);

    // One entry per language for preference pickers, keyed by the 639-2/B code
    // subtitle addons actually emit, in the reference order.
    [[nodiscard]] std::span<LanguageOption const> LanguageOptions() noexcept;
}
