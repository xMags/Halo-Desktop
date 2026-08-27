#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace HaloDesktop::Services::Downloads
{
    // Plain values shared by the thread-safe download engine, persistence
    // store, and the UI-thread-only service facade. These types contain no
    // source URL or request header except ProtectedRequest, which may only be
    // passed to RequestVault and TransferEngine and must never be UI-bound.
    enum class DownloadStatus
    {
        Queued,
        Downloading,
        Paused,
        Done,
        Failed,
    };

    enum class DownloadFailureCode
    {
        SourceExpired,
        StorageFull,
        InvalidRange,
        MissingFile,
        Network,
        ServerUnavailable,
        SourceRejected,
        ProtectedRequestCorrupt,
        Unknown,
    };

    struct DownloadMedia final
    {
        std::wstring VideoId;
        std::wstring ItemId;
        std::wstring MediaType;
        std::optional<std::wstring> MetaId;
        std::wstring Title;
        std::optional<std::wstring> ShowName;
        std::optional<std::wstring> EpisodeLabel;
        std::optional<std::wstring> Poster;
        std::optional<std::wstring> AddonId;
        std::optional<std::wstring> BingeGroup;
        std::optional<std::wstring> FileName;
        std::optional<std::uint64_t> VideoSize;
        std::optional<std::wstring> VideoHash;
        std::optional<std::wstring> StreamName;
        std::optional<std::wstring> StreamTitle;
    };

    struct SubtitleRequest final
    {
        std::wstring Url;
        std::wstring Language;
        std::wstring Id;
        std::map<std::wstring, std::wstring, std::less<>> Headers;
    };

    struct ProtectedRequest final
    {
        std::wstring Url;
        std::map<std::wstring, std::wstring, std::less<>> Headers;
        std::optional<SubtitleRequest> Subtitle;
    };

    struct ReplacementBackup final
    {
        std::wstring JobId;
        std::filesystem::path RootPath;
        std::wstring FileName;
        std::optional<std::wstring> SubtitleFileName;
    };

    struct DownloadRecord final
    {
        std::wstring JobId;
        std::wstring AccountKey;
        DownloadMedia Media;
        std::wstring FileName;
        std::optional<std::wstring> SubtitleFileName;
        std::optional<std::wstring> SubtitleLanguage;
        std::filesystem::path RootPath;
        DownloadStatus Status{ DownloadStatus::Queued };
        std::uint64_t TotalBytes{};
        std::uint64_t DownloadedBytes{};
        std::optional<std::wstring> Validator;
        std::wstring SourceFingerprint;
        std::optional<DownloadFailureCode> Failure;
        bool ExplicitPause{};
        std::uint64_t CreatedAt{};
        std::uint64_t UpdatedAt{};
        std::uint64_t BytesPerSecond{};
        std::optional<ReplacementBackup> Replacement;
        bool PendingDeletion{};

        [[nodiscard]] std::filesystem::path TargetPath() const;
        [[nodiscard]] std::filesystem::path PartialPath() const;
    };

    struct DownloadStartRequest final
    {
        DownloadMedia Media;
        ProtectedRequest Request;
        bool ReplaceExisting{};
    };

    struct PlaybackFiles final
    {
        std::filesystem::path VideoPath;
        std::optional<std::filesystem::path> SubtitlePath;
    };

    struct ContentRange final
    {
        std::uint64_t Start{};
        std::uint64_t End{};
        std::uint64_t Total{};
    };

    [[nodiscard]] bool IsActive(DownloadStatus status) noexcept;
    [[nodiscard]] bool RequiresNewSource(DownloadFailureCode failure) noexcept;
    [[nodiscard]] DownloadStatus RecoverStatus(DownloadStatus status, bool explicitPause) noexcept;
    [[nodiscard]] std::wstring FailureMessage(DownloadFailureCode failure);
    [[nodiscard]] std::wstring Sha256Hex(std::wstring const& value);
    [[nodiscard]] std::wstring MakeAccountKey(
        std::wstring serverUrl,
        std::wstring const& userId);
    [[nodiscard]] std::wstring MakeDownloadFileName(
        DownloadMedia const& media,
        std::wstring const& sourceFingerprint);
    [[nodiscard]] bool IsSafeFileName(std::wstring const& value) noexcept;
    [[nodiscard]] std::optional<ContentRange> ParseContentRange(std::wstring const& value) noexcept;
    [[nodiscard]] bool HasSufficientSpace(
        std::uint64_t availableBytes,
        std::uint64_t videoBytes) noexcept;
    void ValidateProtectedRequest(ProtectedRequest const& request);
}
