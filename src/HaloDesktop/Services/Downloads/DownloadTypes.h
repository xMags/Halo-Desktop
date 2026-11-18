#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
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
        SourceNotReady,
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
        std::optional<std::wstring> LandscapeArtwork;
        std::optional<std::wstring> AddonId;
        std::optional<std::wstring> BingeGroup;
        std::optional<std::wstring> FileName;
        std::optional<std::uint64_t> VideoSize;
        std::optional<std::wstring> VideoHash;
        std::optional<std::wstring> StreamName;
        std::optional<std::wstring> StreamTitle;
        // The dynamic range token the source sheet parsed from the whole stream
        // when the download started (`DV`, `HDR10`, `HLG`, `HDR`). Display only;
        // absent on records written before it was kept, which then fall back to
        // what the stored name and title say.
        std::optional<std::wstring> DynamicRange;
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
        // Connections receiving data right now. Live state only: never stored in
        // the index, and 0 unless the download is downloading.
        std::uint32_t ActiveConnections{};

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

    // What a finished download tells the sources sheet about itself. The local
    // path is deliberately absent: playback goes through BuildPlaybackRequest so
    // the engine can re-check the file still exists and belongs to this account.
    struct CompletedDownloadSource final
    {
        std::wstring JobId;
        // The release name the addon used when the download started, falling back
        // to the sanitized on-disk name, which is never empty.
        std::wstring ReleaseName;
        std::uint64_t SizeBytes{};
    };

    // One download of a video, whatever its state, as the sources sheet reads it:
    // what decides the saved copy card and which source wears a save state. No
    // path, URL or header.
    struct VideoDownload final
    {
        std::wstring JobId;
        // The release file name the download was saved under: the addon's name
        // when the download started, else the sanitized on-disk name.
        std::wstring ReleaseName;
        // The stream's own name and title, which can say what the file name does not.
        std::optional<std::wstring> StreamName;
        std::optional<std::wstring> StreamTitle;
        DownloadStatus Status{ DownloadStatus::Queued };
        std::uint64_t SizeBytes{};
        // Zero to one, or empty while the size is unknown.
        std::optional<double> Fraction;
        // The subtitle saved beside it, if any.
        std::optional<std::wstring> SubtitleLanguage;
    };

    struct ContentRange final
    {
        std::uint64_t Start{};
        std::uint64_t End{};
        std::uint64_t Total{};
    };

    // Season and episode read back out of a label such as "S01E05". Ordering is
    // lexicographic on the pair, which is what puts the next episode next.
    //
    // Labels are written as S%02dE%02d, so the digit runs are at least two long
    // but not capped: a show past its hundredth episode writes "S01E100", and a
    // parser that assumed a fixed width would stop finding the next episode for
    // exactly the long-running shows people download whole seasons of.
    [[nodiscard]] std::optional<std::pair<int, int>> ParseEpisodePosition(
        std::optional<std::wstring> const& label) noexcept;

    [[nodiscard]] bool IsActive(DownloadStatus status) noexcept;
    [[nodiscard]] bool RequiresNewSource(DownloadFailureCode failure) noexcept;
    // A resolver that has not prepared a video yet answers its link with a
    // short stand-in clip, a complete and valid file of a few hundred
    // kilobytes. True when the size the source states is that small against
    // the size the addon gave for the video. Both bounds must hold: the
    // addon's size can be a whole season pack's, so a real episode may be a
    // small share of it, but never a few megabytes of a film-sized stream.
    [[nodiscard]] bool IsStandInForVideo(std::uint64_t statedTotal, std::optional<std::uint64_t> sizeHint) noexcept;
    [[nodiscard]] DownloadStatus RecoverStatus(DownloadStatus status, bool explicitPause) noexcept;
    [[nodiscard]] std::wstring FailureMessage(DownloadFailureCode failure);
    [[nodiscard]] std::wstring Sha256Hex(std::wstring const& value);
    [[nodiscard]] std::wstring MakeAccountKey(
        std::wstring serverUrl,
        std::wstring const& userId);
    // The download folder is shared by every account on the device, so the name
    // carries both the source fingerprint and the account it belongs to. Without
    // the account segment two accounts saving the same source URL resolve to one
    // name, and the second one fails on the existing file it is not allowed to
    // see. Names already in the index keep whatever they were written with.
    [[nodiscard]] std::wstring MakeDownloadFileName(
        DownloadMedia const& media,
        std::wstring const& sourceFingerprint,
        std::wstring const& accountKey);
    [[nodiscard]] bool IsSafeFileName(std::wstring const& value) noexcept;

    // True when the path sits inside one of the approved download roots, compared
    // component by component so a sibling folder with a shared prefix does not
    // match. Purely lexical: callers that are about to delete must resolve
    // reparse points first, or a junction inside a root escapes it.
    [[nodiscard]] bool IsWithinApprovedRoot(
        std::filesystem::path const& candidate,
        std::vector<std::filesystem::path> const& approvedRoots) noexcept;
    [[nodiscard]] std::optional<ContentRange> ParseContentRange(std::wstring const& value) noexcept;

    enum class FilePresenceKind
    {
        Present,
        // The folder is there and the file is not: it was deleted or moved.
        Missing,
        // The folder itself cannot be reached, such as an unplugged drive, so
        // nothing is known about the file.
        FolderUnavailable,
    };

    // What the disk says about a finished download's file.
    struct FilePresence final
    {
        FilePresenceKind Kind{ FilePresenceKind::FolderUnavailable };
        // The file's size when Present.
        std::uint64_t Bytes{};

        friend bool operator==(FilePresence const&, FilePresence const&) = default;
    };

    [[nodiscard]] FilePresence DetectFilePresence(
        std::filesystem::path const& root,
        std::filesystem::path const& target) noexcept;
    [[nodiscard]] bool HasSufficientSpace(
        std::uint64_t availableBytes,
        std::uint64_t videoBytes) noexcept;
    void ValidateProtectedRequest(ProtectedRequest const& request);
}
