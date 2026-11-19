#pragma once

#include "Services/Downloads/DownloadTypes.h"
#include "ViewModels/QualityMedalPolicy.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The reference client's `downloads/downloadsPresentation.ts`: what the
// Downloads page says about each download and about the page as a whole, so
// the page only lays it out. Pure, so the tests include it by path.
namespace HaloDesktop::ViewModels::DownloadsPresentation
{
    using Services::Downloads::DownloadRecord;

    enum class RowState
    {
        Downloading,
        Queued,
        Paused,
        Failed,
        OnDisk,
    };

    // One tone shared by a row's lead line, progress fill and the pane's copy of both.
    enum class RowTone
    {
        Normal,
        Caution,
        Critical,
    };

    // What the release name stated about the file, as the stream parser read it.
    struct StreamFacts final
    {
        std::optional<std::wstring> Quality;
        std::optional<std::wstring> Codec;
        std::optional<std::wstring> DynamicRange;
    };

    struct QualityBadge final
    {
        std::wstring Label;
        bool Gold{};

        bool operator==(QualityBadge const&) const = default;
    };

    struct DownloadRow final
    {
        std::wstring Id;
        // Episode label, else the media type (`MOVIE`), in the row's mono tag.
        std::wstring Tag;
        // The episode label alone, for the badge on an episode's card; empty for a film.
        std::wstring EpisodeLabel;
        std::wstring Name;
        // The episode title under a show, the file name under a film; empty when it would repeat the name.
        std::wstring Sub;
        RowState State{ RowState::Queued };
        double Progress{};
        // The mono lead line: what the transfer is doing now.
        std::wstring Lead;
        RowTone Tone{ RowTone::Normal };
        // `1.2 GB / 4.0 GB`, or the one size a finished file has.
        std::wstring DownloadedLine;
        // The queue row's mono line: `1.2 of 4.0 GB`, percent, then time left or the state.
        std::vector<std::wstring> TransferMeta;
        std::wstring CardTitle;
        // The episode's own title under the show's, empty for a film.
        std::wstring CardSub;
        // `HEVC · 20.0 GB · ENG`; the badge over the art carries the quality.
        std::wstring CardMeta;
        // None when the release named no quality.
        std::optional<QualityBadge> Badge;
        // The tag, and the episode title for an episode.
        std::wstring PaneMeta;
        Medal QualityMedal;
        // `2160p · HEVC`, for the pane's facts.
        std::wstring QualityLine;
        std::wstring SubsChip;
        bool SubsMuted{};
        // The subtitle fact: the language's name, or that there is none.
        std::wstring Subs;
        std::wstring AddedLabel;
        std::wstring FileName;
        std::wstring Poster;
        // The title's id, for the artwork's metahub fallback. Not in the
        // reference's row, whose artwork component takes it separately.
        std::wstring MetaId;
        // Landscape still for the 16:9 art, falling back to the poster.
        std::wstring RowArtwork;
        bool RequiresNewSource{};
        bool CanPause{};
        bool CanResume{};
        bool CanRetry{};
    };

    [[nodiscard]] DownloadRow RowOf(DownloadRecord const& record, StreamFacts const& facts);

    // `ADDED 05 SEP`, from the day the download was created, in local time.
    [[nodiscard]] std::wstring AddedLabel(std::uint64_t createdAtMilliseconds);
    // The pane's "Added" fact drops the row label's prefix.
    [[nodiscard]] std::wstring AddedFact(std::wstring_view label);
    // `1.2 of 4.0 GB` when both sizes share a unit, else both with their own.
    [[nodiscard]] std::wstring BytesOfTotal(std::uint64_t done, std::uint64_t total);
    [[nodiscard]] std::wstring PercentText(double progress);
    // The pane's state label: `DOWNLOADING`, `QUEUED`, `PAUSED`, `FAILED`, `ON DISK`.
    [[nodiscard]] std::wstring StatePillText(RowState state);

    enum class Filter
    {
        All,
        Active,
        Ready,
        Failed,
    };

    // Active is everything not yet on disk, failed transfers included.
    [[nodiscard]] bool MatchesFilter(DownloadRow const& row, Filter filter) noexcept;

    // Where an unfinished transfer sits on the page: moving (or paused by the
    // viewer) in the main list, waiting its turn in the folded "Up next" card,
    // or stopped and needing the viewer, as an error bar above both.
    enum class TransferSection
    {
        Running,
        Waiting,
        Attention,
    };

    [[nodiscard]] TransferSection SectionOf(RowState state) noexcept;

    struct FilterCounts final
    {
        std::size_t All{};
        std::size_t Active{};
        std::size_t Ready{};
        std::size_t Failed{};

        bool operator==(FilterCounts const&) const = default;
    };

    [[nodiscard]] FilterCounts CountFilters(std::span<DownloadRow const> transfers, std::span<DownloadRow const> ready);
    [[nodiscard]] std::wstring NoMatchesLine(Filter filter);

    // The row the detail pane describes: the one the viewer picked if the
    // filter still shows it, else the first ready download, else the first
    // transfer. Null for none.
    [[nodiscard]] DownloadRow const* ResolveSelection(
        std::wstring_view selectedId,
        std::span<DownloadRow const> ready,
        std::span<DownloadRow const> transfers) noexcept;

    // `1 downloading · 1 waiting · 3 ready`, from every download; empty parts
    // are left out, and the line is empty with nothing at all.
    [[nodiscard]] std::wstring SummaryLine(std::span<DownloadRow const> rows);
    // The Ready section's count and the space those files take.
    [[nodiscard]] std::wstring ReadyHeadline(std::size_t count, std::uint64_t bytes);

    // Combined rate of every transfer, in MB/s.
    [[nodiscard]] double AggregateRate(std::span<DownloadRecord const> records) noexcept;
    [[nodiscard]] std::wstring RateText(double rate, bool pausedAll);

    struct RateFigure final
    {
        std::wstring Value;
        // Empty under a bulk pause.
        std::wstring Unit;

        bool operator==(RateFigure const&) const = default;
    };

    [[nodiscard]] RateFigure RateFigureOf(double rate, bool pausedAll);

    // Every running transfer's receiving connections, summed.
    [[nodiscard]] std::uint64_t TotalConnections(std::span<DownloadRecord const> records) noexcept;
    [[nodiscard]] std::wstring ConnectionsLabel(std::uint64_t count);

    enum class RateNoteKind
    {
        // Drawn as the green pill.
        Connections,
        // Quiet text.
        Note,
    };

    struct RateNote final
    {
        RateNoteKind Kind{ RateNoteKind::Note };
        std::wstring Text;

        bool operator==(RateNote const&) const = default;
    };

    [[nodiscard]] RateNote RateNoteOf(std::span<DownloadRecord const> records);

    enum class RateTone
    {
        Normal,
        Idle,
        Paused,
    };

    [[nodiscard]] RateTone RateToneOf(double rate, bool pausedAll) noexcept;
    [[nodiscard]] std::size_t ActiveCount(std::span<DownloadRecord const> records) noexcept;
    [[nodiscard]] std::wstring QueueLine(std::size_t active);
    [[nodiscard]] std::wstring PeakText(std::span<double const> throughput);

    struct ChartBar final
    {
        // Share of the chart's height against the window's own peak; never below the floor.
        double Fraction{};
        // The newest bars are lit; older ones recede towards the start of the minute.
        double Opacity{};
    };

    // The shortest bar, so an idle minute still reads as a whole row.
    inline constexpr double ChartFloor = 0.04;
    // The window's peak reaches this share of the chart, so a steady transfer
    // reads as bars with air above them and a new peak has room to rise.
    inline constexpr double ChartCeiling = 0.88;
    inline constexpr std::size_t LitBars = 4;

    // A fixed row of slots, oldest first; slots before the first sample are empty.
    [[nodiscard]] std::vector<ChartBar> ChartBars(std::span<double const> throughput);

    struct StorageFigures final
    {
        std::uint64_t StoredBytes{};
        std::uint64_t InFlightBytes{};
        std::optional<std::uint64_t> FreeBytes;
        // Shares of the visible capacity (free space plus what is held).
        double UsedFraction{};
        double StoredFraction{};
    };

    [[nodiscard]] StorageFigures StorageFiguresOf(
        std::span<DownloadRecord const> records,
        std::optional<std::uint64_t> freeBytes) noexcept;
    [[nodiscard]] std::wstring FreeLine(std::optional<std::uint64_t> freeBytes);

    struct SplitSize final
    {
        std::wstring Value;
        std::wstring Unit;

        bool operator==(SplitSize const&) const = default;
    };

    // `55.5` drawn large, `GB` beside it.
    [[nodiscard]] SplitSize SplitBytes(std::uint64_t bytes);
}
