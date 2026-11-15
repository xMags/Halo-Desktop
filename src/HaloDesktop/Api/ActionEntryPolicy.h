#pragma once

#include <string_view>

namespace HaloDesktop::Api
{
    // Addon entries that are actions, not titles or videos, and never reach a row.
    //
    // Some addons put destructive actions into their catalogs as fake items: a
    // watchlist row opens with "Delete all files", id `dl<service>`, whose one
    // stream points at a delete endpoint and empties the debrid account when it
    // is played. Halo is a player, so both halves are dropped where addon data
    // enters the app: the catalog entry, and any stream that would act, should a
    // future addon disguise the entry well enough to pass the first check.
    //
    // The three patterns are ported from the reference client's
    // `addon/actionEntries.ts`, including its JavaScript regular expression
    // semantics (ASCII word boundaries, Unicode simple case folding for the
    // text pattern, ASCII-only case folding for the URL pattern).

    // An empty view stands for an absent field; an empty value never matches.
    [[nodiscard]] bool IsActionEntry(std::wstring_view id, std::wstring_view name);

    struct ActionStreamFields final
    {
        std::wstring_view Url;
        std::wstring_view Name;
        std::wstring_view Title;
        std::wstring_view Description;
    };

    [[nodiscard]] bool IsActionStream(ActionStreamFields const& stream);

    // Exposed for tests: the individual reference patterns.
    [[nodiscard]] bool MatchesActionId(std::wstring_view id) noexcept;
    [[nodiscard]] bool MatchesActionText(std::wstring_view text);
    [[nodiscard]] bool MatchesActionUrl(std::wstring_view url) noexcept;
}
