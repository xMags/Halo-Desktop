#pragma once

#include "Api/Dto.h"

#include <vector>

namespace HaloDesktop::Api
{
    // What a manifest catalog's extras allow, read from both the `extra`
    // declarations and the legacy `extraSupported` / `extraRequired` lists, as
    // the reference client reads them.

    // Whether the catalog pages with the `skip` extra; any other is one page.
    [[nodiscard]] bool SupportsSkip(Dto::AddonRecord::Catalog const& catalog);

    // Whether a search can be sent with the query alone: no extra other than
    // `search` is required. A catalog that insists on another extra (a genre,
    // say) cannot answer a search and would only be reported as failing.
    [[nodiscard]] bool SearchNeedsNothingElse(Dto::AddonRecord::Catalog const& catalog);

    // The options of the catalog's `genre` extra, in the addon's order; empty
    // when it declares none or has no such extra.
    [[nodiscard]] std::vector<winrt::hstring> GenreOptions(Dto::AddonRecord::Catalog const& catalog);
}
