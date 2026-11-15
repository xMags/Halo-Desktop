#include "pch.h"
#include "Api/CatalogExtrasPolicy.h"

#include <algorithm>

namespace HaloDesktop::Api
{
    bool SupportsSkip(Dto::AddonRecord::Catalog const& catalog)
    {
        auto const declared = std::any_of(catalog.Extras.begin(), catalog.Extras.end(), [](auto const& extra)
        {
            return extra.Name == L"skip";
        });
        return declared || std::find(catalog.ExtraSupported.begin(), catalog.ExtraSupported.end(), L"skip")
            != catalog.ExtraSupported.end();
    }

    bool SearchNeedsNothingElse(Dto::AddonRecord::Catalog const& catalog)
    {
        auto const declared = std::any_of(catalog.Extras.begin(), catalog.Extras.end(), [](auto const& extra)
        {
            return extra.IsRequired && extra.Name != L"search";
        });
        auto const legacy = std::any_of(catalog.ExtraRequired.begin(), catalog.ExtraRequired.end(), [](auto const& name)
        {
            return name != L"search";
        });
        return !declared && !legacy;
    }

    std::vector<winrt::hstring> GenreOptions(Dto::AddonRecord::Catalog const& catalog)
    {
        auto const genre = std::find_if(catalog.Extras.begin(), catalog.Extras.end(), [](auto const& extra)
        {
            return extra.Name == L"genre";
        });
        return genre == catalog.Extras.end() ? std::vector<winrt::hstring>{} : genre->Options;
    }
}
