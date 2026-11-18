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

    // A local document whose save never reached the server ("unsent") is sent
    // again only while it is strictly newer than the document the server
    // answered with: the same newer-wins rule the server applies to a save. A
    // server document as new or newer replaces it instead. Used both for the
    // document held this session and for the one an earlier session left in
    // the account's mirror.
    [[nodiscard]] bool UnsentSettingsAreNewer(
        bool unsent,
        std::int64_t localUpdatedAt,
        std::int64_t serverUpdatedAt) noexcept;

    // A finished load, as far as sending an unsent document again goes.
    struct SettingsResendCheck final
    {
        // The document came from the server, not the mirror: only then is the
        // server known to be reachable and its timestamp known.
        bool FromServer{};
        // False for the load a failed save starts itself, so a server that
        // answers reads but fails saves is not asked again and again; the
        // next load from elsewhere, or the next edit, sends it.
        bool MayResend{};
        bool Unsent{};
        std::int64_t LocalUpdatedAt{};
        std::int64_t ServerUpdatedAt{};
        std::uint64_t RequestWriteVersion{};
        std::uint64_t CurrentWriteVersion{};
    };

    // Whether a load that kept the local document sends it again. An edit
    // made while the load ran sends the document itself.
    [[nodiscard]] bool ShouldResendUnsentSettings(SettingsResendCheck const& check) noexcept;
}
