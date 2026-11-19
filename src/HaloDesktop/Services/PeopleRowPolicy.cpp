#include "Services/PeopleRowPolicy.h"

#include "Services/SearchRankingPolicy.h"

#include <algorithm>
#include <utility>

namespace HaloDesktop::Services
{
    namespace
    {
        // The row admits a name holding every word typed: the whole name, its
        // start, or a surname alone ("nolan").
        constexpr int LoosestPersonTier = 3;
        constexpr int ExactNameTier = 0;
    }

    std::vector<Api::Dto::PersonSummary> ChoosePeopleRow(
        std::span<Api::Dto::PersonSummary const> people,
        std::wstring_view query,
        std::optional<int> bestTitleTier)
    {
        struct Candidate final
        {
            Api::Dto::PersonSummary const* Person;
            int Tier;
        };
        auto const trimmed = TrimmedText(query);
        std::vector<Candidate> candidates;
        for (auto const& person : people)
        {
            auto const tier = NameRelevance(person.Name, trimmed);
            if (tier > LoosestPersonTier) continue;
            // A title named exactly what was typed is what was meant.
            if (bestTitleTier && *bestTitleTier == ExactNameTier && tier > ExactNameTier) continue;
            candidates.push_back({ &person, tier });
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b)
        {
            if (a.Tier != b.Tier) return a.Tier < b.Tier;
            return a.Person->Popularity > b.Person->Popularity;
        });
        // Beside someone with a photo, a namesake without one is nobody the
        // typist meant; with no photo anywhere, everyone stays.
        auto const hasPhoto = [](Candidate const& candidate)
        {
            return candidate.Person->Photo.has_value() && !candidate.Person->Photo->empty();
        };
        auto const anyPhoto = std::any_of(candidates.begin(), candidates.end(), hasPhoto);
        std::vector<Api::Dto::PersonSummary> row;
        for (auto const& candidate : candidates)
        {
            if (row.size() == PeopleRowLimit) break;
            if (anyPhoto && !hasPhoto(candidate)) continue;
            row.push_back(*candidate.Person);
        }
        return row;
    }

    std::optional<int> BestTitleTier(std::span<SearchTitle const> titles, std::wstring_view query)
    {
        auto const parts = ParseSearchQuery(TrimmedText(query));
        std::optional<int> best;
        for (auto const& title : titles)
        {
            auto const tier = (std::min)(NameRelevance(title.Name, parts.Full), NameRelevance(title.Name, parts.Name));
            if (!best || tier < *best) best = tier;
        }
        return best;
    }

    std::vector<SearchTitle> CreditTitles(std::span<Api::Dto::PersonCredit const> credits)
    {
        std::vector<SearchTitle> titles;
        titles.reserve(credits.size());
        for (auto const& credit : credits)
        {
            std::wstring id{ credit.ImdbId };
            std::wstring type{ credit.Type };
            if (!IsImdbId(id) || credit.Name.empty() || !IsSearchedCatalogType(type)) continue;
            SearchTitle title;
            title.Id = id;
            title.ImdbId = std::move(id);
            title.Type = std::move(type);
            title.Name = std::wstring{ credit.Name };
            if (credit.Poster) title.Poster = std::wstring{ *credit.Poster };
            if (credit.Year) title.ReleaseInfo = std::wstring{ *credit.Year };
            titles.push_back(std::move(title));
        }
        return titles;
    }

    std::optional<std::size_t> ExactNamePerson(
        std::span<Api::Dto::PersonSummary const> people,
        std::wstring_view name)
    {
        auto const trimmed = TrimmedText(name);
        if (trimmed.empty()) return std::nullopt;
        std::optional<std::size_t> chosen;
        for (std::size_t index = 0; index < people.size(); ++index)
        {
            if (NameRelevance(people[index].Name, trimmed) != 0) continue;
            if (!chosen || people[index].Popularity > people[*chosen].Popularity) chosen = index;
        }
        return chosen;
    }

    std::wstring KnownForLabel(Api::Dto::PersonDepartment department)
    {
        switch (department)
        {
        case Api::Dto::PersonDepartment::Acting: return L"Actor";
        case Api::Dto::PersonDepartment::Directing: return L"Director";
        case Api::Dto::PersonDepartment::Writing: return L"Writer";
        case Api::Dto::PersonDepartment::Unknown: break;
        }
        return {};
    }

    std::wstring DepartmentLabel(Api::Dto::PersonDepartment department)
    {
        switch (department)
        {
        case Api::Dto::PersonDepartment::Acting: return L"Acting";
        case Api::Dto::PersonDepartment::Directing: return L"Directing";
        case Api::Dto::PersonDepartment::Writing: return L"Writing";
        case Api::Dto::PersonDepartment::Unknown: break;
        }
        return {};
    }

    std::wstring PersonRowLine(Api::Dto::PersonSummary const& person)
    {
        std::wstring titles;
        for (auto const& title : person.KnownForTitles)
        {
            if (!titles.empty()) titles.append(L", ");
            titles.append(title);
        }
        auto line = KnownForLabel(person.KnownFor);
        if (!line.empty() && !titles.empty()) line.append(L" · ");
        line.append(titles);
        return line;
    }
}
