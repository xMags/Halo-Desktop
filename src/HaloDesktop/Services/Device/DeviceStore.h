#pragma once

#include "Services/Device/DeviceDocuments.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <pplawait.h>
#include <ppltasks.h>

namespace HaloDesktop::Services::Device
{
    // Where the on-device backend keeps its documents (the reference's
    // `DeviceStore`). A document never written reads as empty. Thread-safe;
    // no work happens on the caller's apartment.
    class IDeviceDocumentStore
    {
    public:
        virtual ~IDeviceDocumentStore() = default;

        // The document's UTF-8 text.
        [[nodiscard]] virtual concurrency::task<std::optional<std::string>> ReadAsync(DeviceCollection collection) = 0;
        [[nodiscard]] virtual concurrency::task<void> WriteAsync(DeviceCollection collection, std::string contents) = 0;

    protected:
        IDeviceDocumentStore() = default;
        IDeviceDocumentStore(IDeviceDocumentStore const&) = default;
        IDeviceDocumentStore& operator=(IDeviceDocumentStore const&) = default;
    };

    // The documents on disk, as the reference desktop's `device_store.rs`
    // keeps them: one file per collection in the folder given (the app passes
    // `<LocalState>\device`), named by a fixed enum so no caller can choose a
    // path. On this PC they are the only copy, so:
    //
    // - every write replaces its file atomically (a temporary file, flushed,
    //   then moved over the old one), so a failed write leaves the old file;
    // - a document is at most MaximumDocumentBytes, and a larger write is
    //   refused before anything is touched;
    // - the addon list is DPAPI-protected for the current user, because addon
    //   URLs can carry debrid keys;
    // - a document that exists but cannot be read (too large, not UTF-8,
    //   not decryptable, a profile in the wrong shape) is reported as
    //   ApiError::DeviceDataUnreadable and is never overwritten or deleted
    //   by a read.
    //
    // Each file's reads and writes are serialised across processes by the
    // app's named file lock, as the other stores do.
    class DeviceStore final : public IDeviceDocumentStore
    {
    public:
        // Far above a heavy user's library and history; a write past it is a bug.
        static constexpr std::size_t MaximumDocumentBytes = 32u * 1024u * 1024u;

        explicit DeviceStore(std::filesystem::path folder);

        [[nodiscard]] std::filesystem::path const& Folder() const noexcept;

        [[nodiscard]] concurrency::task<std::optional<std::string>> ReadAsync(DeviceCollection collection) override;
        [[nodiscard]] concurrency::task<void> WriteAsync(DeviceCollection collection, std::string contents) override;

        // The saved profile, or empty when Halo has not been used here without
        // an account.
        [[nodiscard]] concurrency::task<std::optional<DeviceProfile>> ReadProfileAsync();
        // Creates the profile with a new id. Refuses when a profile file is
        // already there, readable or not, so an existing id (which names this
        // PC's downloads) is never replaced.
        [[nodiscard]] concurrency::task<DeviceProfile> CreateProfileAsync(std::int64_t createdAt);
        // Removes every document and then the profile, once their contents
        // live in an account. Missing files are not an error. The profile is
        // removed last, so one that fails or is cut short part way leaves the
        // profile, and with it a move that is tried again.
        [[nodiscard]] concurrency::task<void> ClearAsync();

    private:
        [[nodiscard]] std::filesystem::path PathFor(DeviceCollection collection) const;

        std::filesystem::path m_folder;
    };
}
