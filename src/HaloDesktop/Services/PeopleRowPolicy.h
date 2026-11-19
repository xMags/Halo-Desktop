#pragma once

#include "Api/Dto.h"
#include "Services/SearchResultsPolicy.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace HaloDesktop::Services
{
    // Which people Search shows beside a query's titles, and how a person is
    // described. Pure and single-threaded; no UI types, so the tests compile
    // this by path.

    inline constexpr std::size_t PeopleRowLimit = 3;

    // At most PeopleRowLimit people whose name holds every word of the query
    // (NameRelevance 0 to 3, so a surname alone finds its people), best named
    // first, then most popular. When a title is named exactly what was typed,
    // only people named exactly that stay: "dune" finds the film, not
    // someone called Dune Smith. When anyone admitted has a photo, those
    // without one are left out.
    [[nodiscard]] std::vector<Api::Dto::PersonSummary> ChoosePeopleRow(
        std::span<Api::Dto::PersonSummary const> people,
        std::wstring_view query,
        std::optional<int> bestTitleTier);

    // The best NameRelevance among `titles` for the query, read the way the
    // ranking reads it (the full query, or its name without a year); none
    // for no titles.
    [[nodiscard]] std::optional<int> BestTitleTier(std::span<SearchTitle const> titles, std::wstring_view query);

    // The one person a name stands for: the exact name (NameRelevance 0),
    // the most popular of several. None when nobody has exactly that name.
    [[nodiscard]] std::optional<std::size_t> ExactNamePerson(
        std::span<Api::Dto::PersonSummary const> people,
        std::wstring_view name);

    // A person's films and series as Search titles, in the order given (the
    // server's: best known first). One without an IMDb id, a name or a kind
    // Search shows is left out.
    [[nodiscard]] std::vector<SearchTitle> CreditTitles(std::span<Api::Dto::PersonCredit const> credits);

    // What a person is: "Actor", "Director", "Writer"; empty when unknown.
    [[nodiscard]] std::wstring KnownForLabel(Api::Dto::PersonDepartment department);
    // The department itself, as a person page's kicker: "Acting" and so on.
    [[nodiscard]] std::wstring DepartmentLabel(Api::Dto::PersonDepartment department);
    // The line under a person's name: "Director · Dune, Arrival, Sicario",
    // or whichever half is known.
    [[nodiscard]] std::wstring PersonRowLine(Api::Dto::PersonSummary const& person);
}
