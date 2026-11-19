#include "Services/FilmographyPolicy.h"

#include <algorithm>
#include <array>
#include <utility>

namespace HaloDesktop::Services
{
    namespace
    {
        bool Holds(std::span<Api::Dto::CreditRole const> roles, Api::Dto::CreditRole role) noexcept
        {
            return std::find(roles.begin(), roles.end(), role) != roles.end();
        }

        // The year as a number; mappers admit only four digits.
        int YearOf(Api::Dto::PersonCredit const& credit) noexcept
        {
            if (!credit.Year || credit.Year->size() != 4) return -1;
            int year = 0;
            for (auto const character : *credit.Year)
            {
                if (character < L'0' || character > L'9') return -1;
                year = year * 10 + (character - L'0');
            }
            return year;
        }
    }

    bool HasRole(Api::Dto::PersonCredit const& credit, FilmographyFilter filter) noexcept
    {
        switch (filter)
        {
        case FilmographyFilter::All: return true;
        case FilmographyFilter::Acting: return Holds(credit.Roles, Api::Dto::CreditRole::Cast);
        case FilmographyFilter::Directing: return Holds(credit.Roles, Api::Dto::CreditRole::Director);
        case FilmographyFilter::Writing: return Holds(credit.Roles, Api::Dto::CreditRole::Writer);
        }
        return false;
    }

    std::vector<FilmographyFilter> FilmographyFilters(std::span<Api::Dto::PersonCredit const> credits)
    {
        std::vector<FilmographyFilter> filters{ FilmographyFilter::All };
        for (auto const filter : { FilmographyFilter::Acting, FilmographyFilter::Directing, FilmographyFilter::Writing })
        {
            auto const held = std::any_of(credits.begin(), credits.end(), [filter](Api::Dto::PersonCredit const& credit)
            {
                return HasRole(credit, filter);
            });
            if (held) filters.push_back(filter);
        }
        return filters;
    }

    std::vector<Api::Dto::PersonCredit> Filmography(
        std::span<Api::Dto::PersonCredit const> credits,
        FilmographyFilter filter)
    {
        std::vector<Api::Dto::PersonCredit> kept;
        for (auto const& credit : credits)
        {
            if (HasRole(credit, filter)) kept.push_back(credit);
        }
        std::stable_sort(kept.begin(), kept.end(), [](Api::Dto::PersonCredit const& a, Api::Dto::PersonCredit const& b)
        {
            auto const yearA = YearOf(a);
            auto const yearB = YearOf(b);
            if (yearA != yearB) return yearA > yearB;
            return a.Popularity > b.Popularity;
        });
        return kept;
    }

    std::wstring CreditRoleLabel(std::span<Api::Dto::CreditRole const> roles)
    {
        static constexpr std::array<std::pair<Api::Dto::CreditRole, wchar_t const*>, 3> Labels{ {
            { Api::Dto::CreditRole::Cast, L"Actor" },
            { Api::Dto::CreditRole::Director, L"Director" },
            { Api::Dto::CreditRole::Writer, L"Writer" },
        } };
        std::wstring label;
        for (auto const& [role, text] : Labels)
        {
            if (!Holds(roles, role)) continue;
            if (!label.empty()) label.append(L" · ");
            label.append(text);
        }
        return label;
    }

    std::wstring CreditLine(Api::Dto::PersonCredit const& credit)
    {
        std::wstring line = credit.Year ? std::wstring{ *credit.Year } : std::wstring{};
        auto const roles = CreditRoleLabel(credit.Roles);
        if (!line.empty() && !roles.empty()) line.append(L" · ");
        line.append(roles);
        return line;
    }
}
