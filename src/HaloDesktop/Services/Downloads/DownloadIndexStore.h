#pragma once

#include "Services/Downloads/DownloadTypes.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace HaloDesktop::Services::Downloads
{
    struct DownloadStoragePaths final
    {
        std::filesystem::path DataRoot;
        std::filesystem::path IndexFile;
        std::filesystem::path ConfigFile;
        std::filesystem::path VaultDirectory;
        std::filesystem::path DefaultDownloadDirectory;
    };

    // The index on disk was written by a newer Halo Desktop, in a format this
    // build does not know. It is never rewritten, set aside or emptied: every
    // load, and so every change (each one loads first), fails with this, and
    // the file waits for a build that can read it. The text is shown as it is.
    class NewerDownloadIndexError final : public std::runtime_error
    {
    public:
        NewerDownloadIndexError()
            : std::runtime_error{ "These downloads were saved by a newer version of Halo Desktop. Update Halo Desktop to manage them." }
        {
        }
    };

    // Thread-safe atomic persistence for non-secret download metadata. Save
    // generations are monotonic, so a delayed older snapshot cannot replace a
    // newer state. This class never serializes a source URL or request header.
    class DownloadIndexStore final
    {
    public:
        explicit DownloadIndexStore(std::filesystem::path dataRoot);

        [[nodiscard]] std::vector<DownloadRecord> Load(bool recoverInterrupted = true);
        // True once after a Load that found the index unreadable, set it aside
        // and started from what could be salvaged. Lets the caller report the
        // recovery without holding the store's lock.
        [[nodiscard]] bool TakeLoadRecoveryNotice();
        void Save(std::vector<DownloadRecord> const& records, std::uint64_t generation);
        void Apply(
            std::vector<DownloadRecord> const& upserts,
            std::vector<std::wstring> const& removals = {});
        [[nodiscard]] std::optional<DownloadRecord> SetLandscapeArtwork(
            std::wstring const& jobId,
            std::wstring const& expectedAccount,
            std::wstring artwork);
        // Gives the named records of `from` to `to` in one atomic rewrite of
        // the index, and answers them as written. Only the owner (and the
        // change time) of a record changes: no file is moved or deleted. The
        // change is remembered for the life of this store, so a copy of one
        // of those records taken before the rewrite and saved after it (an
        // engine snapshot that was already on its way) is saved under `to`
        // and cannot give the record back to `from`.
        [[nodiscard]] std::vector<DownloadRecord> AdoptAccount(
            std::wstring const& from,
            std::wstring const& to,
            std::vector<std::wstring> const& jobIds,
            std::uint64_t now);

        [[nodiscard]] std::filesystem::path DownloadDirectory() const;
        void SetDownloadDirectory(std::filesystem::path directory);

        // Every folder this device has used as a download root, newest first.
        // The index itself is plain JSON that any corruption or edit can reach,
        // so file deletion is confined to these folders instead of trusting the
        // root path a record carries. The list is seeded once from the records
        // present at upgrade time, then only grows when a folder is chosen.
        [[nodiscard]] std::vector<std::filesystem::path> ApprovedRoots() const;
        [[nodiscard]] DownloadStoragePaths const& Paths() const noexcept;

    private:
        struct Adoption final
        {
            std::wstring From;
            std::wstring To;
        };

        void WriteConfigLocked() const;
        // An adopted record arriving with its old owner gets its new one.
        void ApplyAdoptionsLocked(DownloadRecord& record) const;

        DownloadStoragePaths m_paths;
        // Job id to the owner change AdoptAccount made, guarded by m_mutex.
        std::map<std::wstring, Adoption, std::less<>> m_adoptions;
        mutable std::mutex m_mutex;
        std::filesystem::path m_downloadDirectory;
        std::vector<std::filesystem::path> m_approvedRoots;
        std::set<std::wstring, std::less<>> m_observedJobIds;
        std::uint64_t m_savedGeneration{};
        bool m_loadRecovered{};
    };
}
