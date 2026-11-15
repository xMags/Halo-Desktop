#pragma once

#include "Api/Dto.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::Services
{
    // The reference client's `browse/similarTitles.ts` and the shelf rules of
    // its "More like this" panel. Pure: no XAML types.

    // One shelf's worth: enough to page through, not a whole catalog.
    inline constexpr std::size_t SimilarLimit = 20;

    // Where a title page's "More like this" shelf comes from.
    struct GenreCatalog final
    {
        std::wstring AddonId;
        std::wstring AddonName;
        std::wstring CatalogId;
        std::wstring Genre;

        bool operator==(GenreCatalog const&) const = default;
    };

    // The first catalog, in resolution order, that lists titles of this type by
    // the title's own genre and can be asked for that genre alone: its leading
    // genre if any addon can filter by it, else the next one. `addons` are the
    // effective addons whose catalogs show; hidden ones must be left out by the
    // caller, as the server leaves their catalogs out.
    [[nodiscard]] std::optional<GenreCatalog> PickGenreCatalog(
        std::span<Api::Dto::AddonRecord const> addons,
        std::wstring_view type,
        std::span<winrt::hstring const> genres);

    // The shelf from a genre catalog's answer: this title and repeats (by type
    // and id) dropped, at most `limit` titles, in the catalog's order.
    [[nodiscard]] std::vector<Api::Dto::MetaPreview> SimilarTitles(
        std::vector<Api::Dto::MetaPreview> items,
        std::wstring_view type,
        std::wstring_view excludeId,
        std::size_t limit = SimilarLimit);
}
