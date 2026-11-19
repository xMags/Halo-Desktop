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
    // the same video: there a finished download beats an unfinished one, a
    // re-download whose replacement backup is finished counts in between (it
    // beats a copy with nothing finished and loses to a finished one), and on a
    // tie the one already in `to` stays. A losing record's replacement backup
    // goes with it, so it cannot resurface as a second copy; a winning record
    // keeps its backup. So a finished file is never discarded while the copy
    // kept for that video has nothing finished. Adopting into the same
    // partition changes nothing.
    [[nodiscard]] AdoptionPlan PlanAdoption(
        std::span<DownloadRecord const> records,
        std::wstring const& from,
        std::wstring const& to);

    // Every partition other than `active` that holds a download, each once,
    // sorted. The files on this PC are its viewer's whichever server address or
    // account saved them: a changed address or a reinstall makes a new account
    // key, and without adoption the downloads under the old key would be hidden
    // and keep their storage. So the active partition adopts all of these.
    [[nodiscard]] std::vector<std::wstring> OtherOwners(
        std::span<DownloadRecord const> records,
        std::wstring const& active);
}
