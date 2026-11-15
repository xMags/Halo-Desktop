#pragma once

#include <cstdint>

namespace HaloDesktop::Services
{
    // A load may only replace the in-memory document when it was issued against
    // the same local-write generation and the server has a strictly newer
    // document timestamp. Equal timestamps deliberately retain the local copy.
    [[nodiscard]] bool ShouldApplyLoadedSettings(
        std::int64_t payloadUpdatedAt,
        std::int64_t localUpdatedAt,
        std::uint64_t requestWriteVersion,
        std::uint64_t currentWriteVersion) noexcept;

    // What a finished settings load found, for the current account.
    struct SettingsLoadResult final
    {
        // A real document (the server's, or the account's local mirror) had
        // already been applied before this load finished.
        bool DocumentAlreadyRead{};
        // This load produced a real document; false when both the server and
        // the mirror failed.
        bool ReceivedDocument{};
        // No newer load was issued after this one.
        bool IsLatestRequest{};
        std::int64_t PayloadUpdatedAt{};
        std::int64_t LocalUpdatedAt{};
        std::uint64_t RequestWriteVersion{};
        std::uint64_t CurrentWriteVersion{};
    };

    enum class SettingsLoadAction
    {
        // Nothing changes.
        Keep,
        // The first real document: it becomes the base every waiting edit is
        // applied on top of before anything is saved.
        AdoptFirstRead,
        // A newer document replaces the local one under the usual rule.
        ApplyNewer,
        // Nothing could be read and no other load is coming: the edits made
        // while waiting are dropped rather than saved over a document never
        // seen, which would replace every other setting with defaults.
        DiscardUnreadEdits,
    };

    [[nodiscard]] SettingsLoadAction DecideSettingsLoad(SettingsLoadResult const& result) noexcept;

    // A save replaces the whole settings document, so it may only be sent once
    // the document it patches has really been read.
    [[nodiscard]] bool CanSaveSettings(bool documentRead) noexcept;
}
