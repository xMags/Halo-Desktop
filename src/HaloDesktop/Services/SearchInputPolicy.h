#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace HaloDesktop::Services
{
    // What the search box holds: words to search the catalogs for, or a link
    // (or bare id) that names one thing directly.
    enum class SearchInputKind
    {
        Text,
        ImdbTitle,
        ImdbPerson,
        TvdbNumeric,
        TvdbSlug,
        Magnet,
        WebLink,
        UnsupportedLink,
    };

    // One classified input. Only the fields of its kind are filled:
    // - ImdbTitle: ImdbId (`tt` and digits, lower case).
    // - ImdbPerson: PersonId (`nm` and digits, lower case).
    // - TvdbNumeric: TvdbId (digits) and TvdbKind (`series` or `movie`).
    // - TvdbSlug: TvdbKind and the slug in DisplayName.
    // - Magnet: InfoHashHex (40 lower-case hex) and DisplayName (the decoded
    //   `dn`, display only). Trackers are never kept.
    // - WebLink: Url (the input, checked), Host (lower case) and DisplayName
    //   (the file name the player shows, else the host).
    // - UnsupportedLink: Reason, a sentence for the viewer.
    struct SearchInput final
    {
        SearchInputKind Kind{ SearchInputKind::Text };
        std::wstring ImdbId;
        std::wstring PersonId;
        std::wstring TvdbId;
        std::wstring TvdbKind;
        std::wstring InfoHashHex;
        std::wstring DisplayName;
        std::wstring Url;
        std::wstring Host;
        std::wstring Reason;

        bool operator==(SearchInput const&) const = default;
    };

    // The longest link that counts as a web link.
    inline constexpr std::size_t SearchWebLinkMaxLength = 2048;
    // The most of a magnet's `dn` that is kept for display.
    inline constexpr std::size_t SearchMagnetNameMaxLength = 200;
    // The most of a link's file name the player shows as its title.
    inline constexpr std::size_t SearchWebLinkTitleMaxLength = 120;

    inline constexpr wchar_t SearchUnsupportedVideoSiteReason[] = L"YouTube and live TV links aren't supported.";

    // Reads `trimmed` (the box's text without surrounding white space). A
    // hand-written parser on purpose: what counts as a web link here is what
    // playback may open, so the rules are spelled out rather than left to a
    // general URI parser. Throws only when memory runs out.
    [[nodiscard]] SearchInput ClassifySearchInput(std::wstring_view trimmed);
}
