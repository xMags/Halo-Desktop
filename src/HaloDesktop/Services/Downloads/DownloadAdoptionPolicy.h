#pragma once

#include "Services/Downloads/DownloadTypes.h"

#include <span>
#include <string>
#include <vector>

namespace HaloDesktop::Services::Downloads
{
    // What moving one partition's downloads into another does (the reference
    // desktop's `plan_adoption` in downloads.rs), for the downloads made
    // without an account when someone signs in. No I/O.
    struct AdoptionPlan final
    {
        // Records that change owner, sorted by job id.
        std::vector<std::wstring> Rekey;
        // Duplicates removed, files included, sorted by job id.
        std::vector<std::wstring> Discard;

        friend bool operator==(AdoptionPlan const&, AdoptionPlan const&) = default;
    };

    // Every record of `from` moves to `to`, except where `to` already shows
    // the same video: there a finished download beats an unfinished one, and
    // when both are finished (or neither is) the one already in `to` stays. A
    // losing record's replacement backup goes with it, so it cannot resurface
    // as a second copy. Adopting into the same partition changes nothing.
    [[nodiscard]] AdoptionPlan PlanAdoption(
        std::span<DownloadRecord const> records,
        std::wstring const& from,
        std::wstring const& to);
}
