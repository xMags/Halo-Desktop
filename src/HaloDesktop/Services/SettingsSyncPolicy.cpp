#include "pch.h"
#include "Services/SettingsSyncPolicy.h"

namespace HaloDesktop::Services
{
    bool ShouldApplyLoadedSettings(
        std::int64_t payloadUpdatedAt,
        std::int64_t localUpdatedAt,
        std::uint64_t requestWriteVersion,
        std::uint64_t currentWriteVersion) noexcept
    {
        return requestWriteVersion == currentWriteVersion
            && payloadUpdatedAt > localUpdatedAt;
    }

    SettingsLoadAction DecideSettingsLoad(SettingsLoadResult const& result) noexcept
    {
        if (!result.DocumentAlreadyRead)
        {
            // Edits made before the first read never advance the write
            // generation (they wait instead of saving), so the first document
            // is adopted whatever its timestamp, and the edits land on it.
            if (result.ReceivedDocument) return SettingsLoadAction::AdoptFirstRead;
            return result.IsLatestRequest ? SettingsLoadAction::DiscardUnreadEdits : SettingsLoadAction::Keep;
        }
        if (!result.ReceivedDocument) return SettingsLoadAction::Keep;
        return ShouldApplyLoadedSettings(
                   result.PayloadUpdatedAt,
                   result.LocalUpdatedAt,
                   result.RequestWriteVersion,
                   result.CurrentWriteVersion)
            ? SettingsLoadAction::ApplyNewer
            : SettingsLoadAction::Keep;
    }

    bool CanSaveSettings(bool documentRead) noexcept
    {
        return documentRead;
    }

    bool UnsentSettingsAreNewer(
        bool unsent,
        std::int64_t localUpdatedAt,
        std::int64_t serverUpdatedAt) noexcept
    {
        return unsent && localUpdatedAt > serverUpdatedAt;
    }

    bool ShouldResendUnsentSettings(SettingsResendCheck const& check) noexcept
    {
        return check.FromServer
            && check.MayResend
            && check.RequestWriteVersion == check.CurrentWriteVersion
            && UnsentSettingsAreNewer(check.Unsent, check.LocalUpdatedAt, check.ServerUpdatedAt);
    }
}
