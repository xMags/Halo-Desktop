#include "Services/SimilarTitlesPolicy.h"

#include <algorithm>
#include <unordered_set>

namespace
{
    constexpr std::wstring_view GenreExtra = L"genre";

    // Whether a catalog can be asked for this one genre with nothing else required.
    bool AcceptsGenreAlone(HaloDesktop::Api::Dto::AddonRecord::Catalog const& catalog, std::wstring_view genre)
    {
        auto const declared = std::find_if(catalog.Extras.begin(), catalog.Extras.end(), [](auto const& extra)
        {
            return std::wstring_view{ extra.Name } == GenreExtra;
        });
        auto const legacy = std::any_of(catalog.ExtraSupported.begin(), catalog.ExtraSupported.end(), [](auto const& name)
        {
            return std::wstring_view{ name } == GenreExtra;
        });
        if (declared == catalog.Extras.end() && !legacy) return false;
        // A catalog that lists its genres only answers for those.
        if (declared != catalog.Extras.end() && !declared->Options.empty())
        {
            auto const listed = std::any_of(declared->Options.begin(), declared->Options.end(), [genre](auto const& option)
            {
                return std::wstring_view{ option } == genre;
            });
            if (!listed) return false;
        }
        auto const otherDeclared = std::any_of(catalog.Extras.begin(), catalog.Extras.end(), [](auto const& extra)
        {
            return extra.IsRequired && std::wstring_view{ extra.Name } != GenreExtra;
        });
        auto const otherLegacy = std::any_of(catalog.ExtraRequired.begin(), catalog.ExtraRequired.end(), [](auto const& name)
        {
            return std::wstring_view{ name } != GenreExtra;
        });
        return !otherDeclared && !otherLegacy;
    }
}

namespace HaloDesktop::Services
{
    std::optional<GenreCatalog> PickGenreCatalog(
        std::span<Api::Dto::AddonRecord const> addons,
        std::wstring_view type,
        std::span<winrt::hstring const> genres)
    {
        for (auto const& genre : genres)
        {
            std::wstring_view const wanted{ genre };
            for (auto const& addon : addons)
            {
                auto const catalog = std::find_if(addon.Catalogs.begin(), addon.Catalogs.end(), [type, wanted](auto const& candidate)
                {
                    return std::wstring_view{ candidate.Type } == type && AcceptsGenreAlone(candidate, wanted);
                });
                if (catalog != addon.Catalogs.end())
                {
                    return GenreCatalog{
                        std::wstring{ addon.Id },
                        std::wstring{ addon.Name },
                        std::wstring{ catalog->Id },
                        std::wstring{ wanted },
                    };
                }
            }
        }
        return std::nullopt;
    }

    std::vector<Api::Dto::MetaPreview> SimilarTitles(
        std::vector<Api::Dto::MetaPreview> items,
        std::wstring_view type,
        std::wstring_view excludeId,
        std::size_t limit)
    {
        // Unit separator: no type or id can hold one, the mappers having
        // stripped control characters.
        auto const key = [](std::wstring_view itemType, std::wstring_view id)
        {
            std::wstring result{ itemType };
            result.push_back(L'\x1F');
            result.append(id);
            return result;
        };
        std::unordered_set<std::wstring> seen{ key(type, excludeId) };
        std::vector<Api::Dto::MetaPreview> result;
        for (auto& item : items)
        {
            if (result.size() == limit) break;
            // Catalogs can repeat a title; a shelf must not draw it twice.
            if (!seen.insert(key(std::wstring_view{ item.Type }, std::wstring_view{ item.Id })).second) continue;
            result.push_back(std::move(item));
        }
        return result;
    }
}
