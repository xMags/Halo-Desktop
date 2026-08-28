#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace HaloDesktop::Services
{
    // One row of the merged global-then-user addon list, reduced to the fields
    // that decide whether two rows name the same addon.
    struct AddonIdentity final
    {
        std::wstring ManifestId;
        bool IsGlobal{};
    };

    // The administrator's global list and a user's own list can both name the
    // same addon, and the server returns a row for each. Building content from
    // every row queries that addon's catalogs twice, so callers asking "which
    // addons provide content?" take one row per manifest id.
    //
    // The user's row wins: it carries the catalog-visibility flag they can
    // actually change, whereas a global row is editable only by an administrator.
    // The winner keeps the first occurrence's slot, so shelf order does not shift
    // when a user installs an addon that is already global.
    //
    // Identity is the manifest id rather than the transport URL because the
    // server redacts global transport URLs from non-administrators (they can
    // embed the administrator's debrid keys), which is exactly the case that
    // needs collapsing. A row with no manifest id is never merged: an addon that
    // failed to declare an identity must not absorb an unrelated one.
    [[nodiscard]] std::vector<std::size_t> SelectDistinctAddons(
        std::span<AddonIdentity const> addons);
} // namespace HaloDesktop::Services
