#include "pch.h"
#include "Services/Downloads/DownloadAdoptionPolicy.h"

#include <algorithm>
#include <set>

namespace
{
    namespace Downloads = HaloDesktop::Services::Downloads;

    // A record that some live record names as its replacement backup.
    bool IsHiddenBackup(std::span<Downloads::DownloadRecord const> records, std::wstring const& jobId)
    {
        return std::any_of(records.begin(), records.end(), [&jobId](Downloads::DownloadRecord const& record)
        {
            return !record.PendingDeletion && record.Replacement && record.Replacement->JobId == jobId;
        });
    }

    bool Visible(
        std::span<Downloads::DownloadRecord const> records,
        Downloads::DownloadRecord const& record,
        std::wstring const& account)
    {
        return record.AccountKey == account && !record.PendingDeletion && !IsHiddenBackup(records, record.JobId);
    }

    // How much of a finished file a visible record stands for: 2 when it is
    // finished itself; 1 when it is a re-download whose backup (the copy it
    // replaces, hidden until it completes) is finished; 0 when nothing is.
    int FinishedRank(std::span<Downloads::DownloadRecord const> records, Downloads::DownloadRecord const& record)
    {
        if (record.Status == Downloads::DownloadStatus::Done) return 2;
        if (!record.Replacement) return 0;
        auto const& backupId = record.Replacement->JobId;
        auto const backup = std::find_if(records.begin(), records.end(), [&backupId](Downloads::DownloadRecord const& candidate)
        {
            return candidate.JobId == backupId;
        });
        return backup != records.end() && !backup->PendingDeletion && backup->Status == Downloads::DownloadStatus::Done ? 1 : 0;
    }
}

namespace HaloDesktop::Services::Downloads
{
    AdoptionPlan PlanAdoption(
        std::span<DownloadRecord const> records,
        std::wstring const& from,
        std::wstring const& to)
    {
        if (from == to)
        {
            return {};
        }
        std::set<std::wstring, std::less<>> discard;
        for (auto const& moving : records)
        {
            if (!Visible(records, moving, from)) continue;
            auto const existing = std::find_if(records.begin(), records.end(), [&records, &to, &moving](DownloadRecord const& candidate)
            {
                return Visible(records, candidate, to) && candidate.Media.VideoId == moving.Media.VideoId;
            });
            if (existing == records.end()) continue;
            // A re-download with a finished backup counts as holding a finished
            // file: losing would discard that backup with it, so it beats a copy
            // with nothing finished, and only a finished copy beats it. When
            // neither holds anything finished, the one further along wins, so a
            // nearly complete download never gives way to a barely started one.
            // Any other tie keeps the copy already in `to`.
            auto const movingRank = FinishedRank(records, moving);
            auto const existingRank = FinishedRank(records, *existing);
            auto const movingWins = movingRank != existingRank
                ? movingRank > existingRank
                : movingRank == 0 && moving.DownloadedBytes > existing->DownloadedBytes;
            auto const& loser = movingWins ? *existing : moving;
            discard.insert(loser.JobId);
            if (loser.Replacement)
            {
                discard.insert(loser.Replacement->JobId);
            }
        }
        AdoptionPlan plan;
        for (auto const& record : records)
        {
            if (record.AccountKey == from && !discard.contains(record.JobId))
            {
                plan.Rekey.push_back(record.JobId);
            }
        }
        std::sort(plan.Rekey.begin(), plan.Rekey.end());
        plan.Discard.assign(discard.begin(), discard.end());
        return plan;
    }

    std::vector<std::wstring> OtherOwners(std::span<DownloadRecord const> records, std::wstring const& active)
    {
        std::set<std::wstring> owners;
        for (auto const& record : records)
        {
            if (!record.AccountKey.empty() && record.AccountKey != active)
            {
                owners.insert(record.AccountKey);
            }
        }
        return { owners.begin(), owners.end() };
    }
}
