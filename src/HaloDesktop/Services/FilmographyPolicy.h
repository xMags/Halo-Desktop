#pragma once

#include "Api/Dto.h"

#include <span>
#include <string>
#include <vector>

namespace HaloDesktop::Services
{
    // How a person page lists their films and series. Pure and
    // single-threaded; no UI types, so the tests compile this by path.

    // The role chips: every credit, or only those in one role.
    enum class FilmographyFilter
    {
        All,
        Acting,
        Directing,
        Writing,
    };

    [[nodiscard]] bool HasRole(Api::Dto::PersonCredit const& credit, FilmographyFilter filter) noexcept;
    // All, then each role at least one credit has, in chip order (Acting,
    // Directing, Writing).
    [[nodiscard]] std::vector<FilmographyFilter> FilmographyFilters(std::span<Api::Dto::PersonCredit const> credits);
    // The credits the filter keeps, newest first; those without a year last.
    // Within a year, the more popular first; otherwise as given.
    [[nodiscard]] std::vector<Api::Dto::PersonCredit> Filmography(
        std::span<Api::Dto::PersonCredit const> credits,
        FilmographyFilter filter);
    // "Actor · Director · Writer", in that order, for the roles held.
    [[nodiscard]] std::wstring CreditRoleLabel(std::span<Api::Dto::CreditRole const> roles);
    // The line under a credit's poster: "2021 · Director · Writer", or the
    // roles alone without a year.
    [[nodiscard]] std::wstring CreditLine(Api::Dto::PersonCredit const& credit);
}
