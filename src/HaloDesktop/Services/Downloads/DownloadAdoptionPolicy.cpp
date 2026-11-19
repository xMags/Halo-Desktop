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
            auto const movingWins = moving.Status == DownloadStatus::Done && existing->Status != DownloadStatus::Done;
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
