#include "ViewModels/DownloadsPresentationPolicy.h"

#include "Controls/ArtworkUrlPolicy.h"
#include "Services/DownloadsLogicPolicy.h"
#include "Services/LanguagePolicy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cwctype>
#include <numeric>
#include <utility>

namespace
{
    namespace Logic = HaloDesktop::Services::DownloadsLogic;
    using HaloDesktop::Services::Downloads::DownloadFailureCode;
    using HaloDesktop::Services::Downloads::DownloadRecord;
    using HaloDesktop::Services::Downloads::DownloadStatus;
    using HaloDesktop::ViewModels::DownloadsPresentation::RowState;
    using HaloDesktop::ViewModels::DownloadsPresentation::RowTone;

    constexpr std::array<wchar_t const*, 12> Months{
        L"JAN", L"FEB", L"MAR", L"APR", L"MAY", L"JUN", L"JUL", L"AUG", L"SEP", L"OCT", L"NOV", L"DEC" };

    constexpr std::wstring_view Separator = L" · ";

    std::wstring Upper(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towupper(character));
        });
        return value;
    }

    std::wstring Join(std::vector<std::wstring> const& parts, std::wstring_view separator)
    {
        std::wstring result;
        for (auto const& part : parts)
        {
            if (part.empty()) continue;
            if (!result.empty()) result += separator;
            result += part;
        }
        return result;
    }

    RowState StateOf(DownloadStatus status) noexcept
    {
        switch (status)
        {
        case DownloadStatus::Queued: return RowState::Queued;
        case DownloadStatus::Downloading: return RowState::Downloading;
        case DownloadStatus::Paused: return RowState::Paused;
        case DownloadStatus::Done: return RowState::OnDisk;
        case DownloadStatus::Failed: return RowState::Failed;
        }
        return RowState::Failed;
    }

    std::wstring LeadOf(DownloadRecord const& record)
    {
        switch (record.Status)
        {
        case DownloadStatus::Failed:
            return HaloDesktop::Services::Downloads::FailureMessage(record.Failure.value_or(DownloadFailureCode::Unknown));
        case DownloadStatus::Done: return L"READY FOR OFFLINE PLAYBACK";
        case DownloadStatus::Paused: return L"PAUSED BY YOU";
        case DownloadStatus::Queued: return L"WAITING FOR THE CURRENT TRANSFER";
        case DownloadStatus::Downloading:
            return record.BytesPerSecond > 0
                ? Logic::FormatDownloadBytes(static_cast<double>(record.BytesPerSecond)) + L"/s"
                : std::wstring{ L"WAITING" };
        }
        return {};
    }

    RowTone ToneOf(RowState state) noexcept
    {
        if (state == RowState::Paused) return RowTone::Caution;
        if (state == RowState::Failed) return RowTone::Critical;
        return RowTone::Normal;
    }

    // The size the transfer agreed with the source, else what the addon promised.
    std::uint64_t TotalOf(DownloadRecord const& record) noexcept
    {
        return record.TotalBytes > 0 ? record.TotalBytes : record.Media.VideoSize.value_or(0);
    }

    // A moving transfer has its time left, the others say why they are not
    // moving; a failure's sentence stands alone, being longer than the line
    // has room for beside anything else.
    std::vector<std::wstring> TransferMetaOf(DownloadRecord const& record, std::uint64_t total, double progress)
    {
        namespace Presentation = HaloDesktop::ViewModels::DownloadsPresentation;
        if (record.Status == DownloadStatus::Failed) return { LeadOf(record) };
        std::vector<std::wstring> parts;
        if (total > 0)
        {
            parts.push_back(Presentation::BytesOfTotal(record.DownloadedBytes, total));
            parts.push_back(Presentation::PercentText(progress));
        }
        else
        {
            parts.push_back(Logic::FormatDownloadBytes(static_cast<double>(record.DownloadedBytes)) + L" so far");
        }
        if (record.Status == DownloadStatus::Downloading)
        {
            if (auto const eta = Logic::DownloadEtaSeconds(total, record.DownloadedBytes, record.BytesPerSecond))
            {
                parts.push_back(Logic::FormatDownloadEta(*eta));
            }
            else if (record.BytesPerSecond == 0)
            {
                parts.push_back(L"Waiting for the source");
            }
        }
        else if (record.Status == DownloadStatus::Paused)
        {
            parts.push_back(L"Paused by you");
        }
        return parts;
    }

    std::wstring DownloadedLineOf(DownloadRecord const& record, std::uint64_t total)
    {
        auto const done = record.Status == DownloadStatus::Done;
        if (total == 0)
        {
            return done
                ? std::wstring{ L"SIZE UNKNOWN" }
                : L"SIZE UNKNOWN · " + Logic::FormatDownloadBytes(static_cast<double>(record.DownloadedBytes));
        }
        if (done) return Logic::FormatDownloadBytes(static_cast<double>(total));
        return Logic::FormatDownloadBytes(static_cast<double>(record.DownloadedBytes))
            + L" / " + Logic::FormatDownloadBytes(static_cast<double>(total));
    }

    std::wstring OneDecimal(double value)
    {
        return Logic::FormatOneDecimal(value);
    }

    // The number and the unit of a formatted size, split at its one space.
    std::pair<std::wstring, std::wstring> SplitFormatted(std::wstring const& formatted)
    {
        auto const space = formatted.find(L' ');
        if (space == std::wstring::npos) return { formatted, {} };
        return { formatted.substr(0, space), formatted.substr(space + 1) };
    }
}

namespace HaloDesktop::ViewModels::DownloadsPresentation
{
    std::wstring AddedLabel(std::uint64_t createdAtMilliseconds)
    {
        if (createdAtMilliseconds == 0) return L"ADDED UNKNOWN";
        auto const time = static_cast<std::time_t>(createdAtMilliseconds / 1000);
        std::tm local{};
        if (localtime_s(&local, &time) != 0 || local.tm_mon < 0 || local.tm_mon > 11) return L"ADDED UNKNOWN";
        std::array<wchar_t, 32> buffer{};
        std::swprintf(buffer.data(), buffer.size(), L"ADDED %02d %ls", local.tm_mday, Months[static_cast<std::size_t>(local.tm_mon)]);
        return std::wstring{ buffer.data() };
    }

    std::wstring AddedFact(std::wstring_view label)
    {
        constexpr std::wstring_view prefix{ L"ADDED " };
        return std::wstring{ label.starts_with(prefix) ? label.substr(prefix.size()) : label };
    }

    std::wstring BytesOfTotal(std::uint64_t done, std::uint64_t total)
    {
        auto const doneText = Logic::FormatDownloadBytes(static_cast<double>(done));
        auto const totalText = Logic::FormatDownloadBytes(static_cast<double>(total));
        auto const [doneValue, doneUnit] = SplitFormatted(doneText);
        if (!doneUnit.empty() && doneUnit == SplitFormatted(totalText).second) return doneValue + L" of " + totalText;
        return doneText + L" of " + totalText;
    }

    std::wstring PercentText(double progress)
    {
        auto const percent = std::isfinite(progress) ? std::floor(progress * 100.0 + 0.5) : 0.0;
        return std::to_wstring(static_cast<long long>(percent)) + L"%";
    }

    std::wstring StatePillText(RowState state)
    {
        switch (state)
        {
        case RowState::Downloading: return L"DOWNLOADING";
        case RowState::Queued: return L"QUEUED";
        case RowState::Paused: return L"PAUSED";
        case RowState::Failed: return L"FAILED";
        case RowState::OnDisk: return L"ON DISK";
        }
        return {};
    }

    DownloadRow RowOf(DownloadRecord const& record, StreamFacts const& facts)
    {
        auto const& media = record.Media;
        auto const state = StateOf(record.Status);
        auto const total = TotalOf(record);
        auto const quality = facts.Quality.value_or(std::wstring{ NotParsedToken });
        auto const medal = MedalFor(quality, facts.DynamicRange.value_or(L"SDR"));
        auto const name = media.ShowName.value_or(media.Title);
        auto const sub = media.ShowName ? media.Title : record.FileName;
        auto const newSource = record.Status == DownloadStatus::Failed && Logic::RequiresNewSource(record.Failure);
        auto const retryable = record.Status == DownloadStatus::Failed && !newSource;
        auto const language = record.SubtitleLanguage && !record.SubtitleLanguage->empty()
            ? record.SubtitleLanguage
            : std::nullopt;
        auto const progress = state == RowState::OnDisk
            ? 1.0
            : (total > 0 ? (std::min)(1.0, static_cast<double>(record.DownloadedBytes) / static_cast<double>(total)) : 0.0);
        auto const shownSub = sub == name ? std::wstring{} : sub;
        auto const downloadedLine = DownloadedLineOf(record, total);
        auto tag = media.EpisodeLabel && !media.EpisodeLabel->empty() ? *media.EpisodeLabel : Upper(media.MediaType);
        if (tag.empty()) tag = L"VIDEO";

        DownloadRow row;
        row.Id = record.JobId;
        row.Tag = tag;
        row.EpisodeLabel = media.ShowName ? media.EpisodeLabel.value_or(L"") : std::wstring{};
        row.Name = name;
        row.Sub = shownSub;
        row.State = state;
        row.Progress = progress;
        row.Lead = LeadOf(record);
        row.Tone = ToneOf(state);
        row.DownloadedLine = downloadedLine;
        row.TransferMeta = TransferMetaOf(record, total, progress);
        row.CardTitle = name;
        row.CardSub = media.ShowName ? shownSub : std::wstring{};
        // Only what the release name stated: an unparsed codec is left out rather
        // than printed as UNKNOWN, and the quality is the badge's, not repeated.
        row.CardMeta = Join({ facts.Codec.value_or(L""), downloadedLine, language ? Upper(*language) : std::wstring{ L"NO SUBS" } }, Separator);
        if (facts.Quality)
        {
            row.Badge = QualityBadge{
                .Label = medal.Tag.empty() ? medal.Plate : medal.Plate + L" " + medal.Tag,
                .Gold = medal.Finish == MedalFinish::Gold,
            };
        }
        row.PaneMeta = media.ShowName && !shownSub.empty() ? tag + std::wstring{ Separator } + shownSub : tag;
        row.QualityMedal = medal;
        row.QualityLine = Join({ facts.Quality.value_or(L""), facts.Codec.value_or(L"") }, Separator);
        if (row.QualityLine.empty()) row.QualityLine = std::wstring{ NotParsedToken };
        row.SubsChip = language ? L"SUB " + Upper(*language) : std::wstring{ L"NO SUBS" };
        row.SubsMuted = !language;
        row.Subs = language ? Services::LanguageLabel(*language) : std::wstring{ L"None" };
        row.AddedLabel = AddedLabel(record.CreatedAt);
        row.FileName = record.FileName;
        row.Poster = media.Poster.value_or(L"");
        row.MetaId = media.MetaId.value_or(L"");
        row.RowArtwork = media.LandscapeArtwork && !media.LandscapeArtwork->empty() ? *media.LandscapeArtwork : row.Poster;
        row.SavedArtwork = ::HaloDesktop::Controls::FileImageUrl(record.SidecarPath(Services::Downloads::SidecarKind::Art));
        row.SavedLogo = ::HaloDesktop::Controls::FileImageUrl(record.SidecarPath(Services::Downloads::SidecarKind::Logo));
        row.OnlineLogo = ::HaloDesktop::Controls::MetahubLogoUrl(row.MetaId).value_or(L"");
        row.RequiresNewSource = newSource;
        row.CanPause = Services::Downloads::IsActive(record.Status);
        row.CanResume = record.Status == DownloadStatus::Paused || retryable;
        row.CanRetry = retryable;
        return row;
    }

    bool MatchesFilter(DownloadRow const& row, Filter filter) noexcept
    {
        switch (filter)
        {
        case Filter::Active: return row.State != RowState::OnDisk;
        case Filter::Ready: return row.State == RowState::OnDisk;
        case Filter::Failed: return row.State == RowState::Failed;
        case Filter::All: return true;
        }
        return true;
    }

    TransferSection SectionOf(RowState state) noexcept
    {
        switch (state)
        {
        case RowState::Queued: return TransferSection::Waiting;
        case RowState::Failed: return TransferSection::Attention;
        case RowState::Downloading:
        case RowState::Paused:
        case RowState::OnDisk:
            return TransferSection::Running;
        }
        return TransferSection::Running;
    }

    FilterCounts CountFilters(std::span<DownloadRow const> transfers, std::span<DownloadRow const> ready)
    {
        return FilterCounts{
            .All = transfers.size() + ready.size(),
            .Active = transfers.size(),
            .Ready = ready.size(),
            .Failed = static_cast<std::size_t>(std::count_if(transfers.begin(), transfers.end(), [](DownloadRow const& row)
            {
                return row.State == RowState::Failed;
            })),
        };
    }

    std::wstring NoMatchesLine(Filter filter)
    {
        switch (filter)
        {
        case Filter::Active: return L"No active transfers right now.";
        case Filter::Ready: return L"No downloads are ready to watch yet.";
        case Filter::Failed: return L"No failed transfers need attention.";
        case Filter::All: return L"No downloads match this filter right now.";
        }
        return L"No downloads match this filter right now.";
    }

    DownloadRow const* ResolveSelection(
        std::wstring_view selectedId,
        std::span<DownloadRow const> ready,
        std::span<DownloadRow const> transfers) noexcept
    {
        auto const byId = [selectedId](DownloadRow const& row) { return row.Id == selectedId; };
        if (!selectedId.empty())
        {
            if (auto const found = std::find_if(ready.begin(), ready.end(), byId); found != ready.end()) return &*found;
            if (auto const found = std::find_if(transfers.begin(), transfers.end(), byId); found != transfers.end()) return &*found;
        }
        if (!ready.empty()) return &ready.front();
        if (!transfers.empty()) return &transfers.front();
        return nullptr;
    }

    std::wstring SummaryLine(std::span<DownloadRow const> rows)
    {
        auto const count = [rows](RowState state)
        {
            return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(), [state](DownloadRow const& row)
            {
                return row.State == state;
            }));
        };
        std::array<std::pair<std::size_t, wchar_t const*>, 5> const parts{ {
            { count(RowState::Downloading), L"downloading" },
            { count(RowState::Queued), L"waiting" },
            { count(RowState::Paused), L"paused" },
            { count(RowState::Failed), L"failed" },
            { count(RowState::OnDisk), L"ready" },
        } };
        std::vector<std::wstring> shown;
        for (auto const& [value, label] : parts)
        {
            if (value > 0) shown.push_back(std::to_wstring(value) + L" " + label);
        }
        return Join(shown, Separator);
    }

    std::wstring ReadyHeadline(std::size_t count, std::uint64_t bytes)
    {
        return std::to_wstring(count) + std::wstring{ Separator } + Logic::FormatDownloadBytes(static_cast<double>(bytes));
    }

    double AggregateRate(std::span<DownloadRecord const> records) noexcept
    {
        double total{};
        for (auto const& record : records) total += static_cast<double>(record.BytesPerSecond);
        return total / (1024.0 * 1024.0);
    }

    std::wstring RateText(double rate, bool pausedAll)
    {
        return pausedAll ? std::wstring{ L"PAUSED" } : OneDecimal(rate) + L" MB/s";
    }

    RateFigure RateFigureOf(double rate, bool pausedAll)
    {
        if (pausedAll) return RateFigure{ .Value = L"PAUSED", .Unit = {} };
        return RateFigure{ .Value = OneDecimal(rate), .Unit = L"MB/s" };
    }

    std::uint64_t TotalConnections(std::span<DownloadRecord const> records) noexcept
    {
        std::uint64_t total{};
        for (auto const& record : records)
        {
            if (record.Status == DownloadStatus::Downloading) total += record.ActiveConnections;
        }
        return total;
    }

    std::wstring ConnectionsLabel(std::uint64_t count)
    {
        return std::to_wstring(count) + (count == 1 ? L" connection" : L" connections");
    }

    // The live connection count while something downloads, a quiet note
    // otherwise. A transfer still opening its first connection says so rather
    // than claiming zero connections.
    RateNote RateNoteOf(std::span<DownloadRecord const> records)
    {
        auto const transfers = std::count_if(records.begin(), records.end(), [](DownloadRecord const& record)
        {
            return record.Status != DownloadStatus::Done;
        });
        if (transfers == 0) return RateNote{ .Kind = RateNoteKind::Note, .Text = L"Idle" };
        auto const downloading = std::any_of(records.begin(), records.end(), [](DownloadRecord const& record)
        {
            return record.Status == DownloadStatus::Downloading;
        });
        if (!downloading) return RateNote{ .Kind = RateNoteKind::Note, .Text = QueueLine(ActiveCount(records)) };
        auto const connections = TotalConnections(records);
        if (connections == 0) return RateNote{ .Kind = RateNoteKind::Note, .Text = L"Connecting" };
        return RateNote{ .Kind = RateNoteKind::Connections, .Text = ConnectionsLabel(connections) };
    }

    RateTone RateToneOf(double rate, bool pausedAll) noexcept
    {
        if (pausedAll) return RateTone::Paused;
        return rate > 0.0 ? RateTone::Normal : RateTone::Idle;
    }

    std::size_t ActiveCount(std::span<DownloadRecord const> records) noexcept
    {
        return static_cast<std::size_t>(std::count_if(records.begin(), records.end(), [](DownloadRecord const& record)
        {
            return Services::Downloads::IsActive(record.Status);
        }));
    }

    std::wstring QueueLine(std::size_t active)
    {
        if (active == 0) return L"No active transfers";
        return std::to_wstring(active) + (active == 1 ? L" active transfer" : L" active transfers");
    }

    std::wstring PeakText(std::span<double const> throughput)
    {
        auto peak = 0.0;
        for (auto const value : throughput)
        {
            if (std::isfinite(value)) peak = (std::max)(peak, value);
        }
        return L"PEAK " + OneDecimal(peak) + L" MB/S";
    }

    std::vector<ChartBar> ChartBars(std::span<double const> throughput)
    {
        auto const slots = Logic::ThroughputSlots;
        auto const taken = throughput.size() > slots ? throughput.subspan(throughput.size() - slots) : throughput;
        std::vector<double> samples(slots - taken.size(), 0.0);
        samples.insert(samples.end(), taken.begin(), taken.end());
        auto peak = 0.0;
        for (auto const value : samples)
        {
            if (std::isfinite(value)) peak = (std::max)(peak, value);
        }
        std::vector<ChartBar> bars;
        bars.reserve(slots);
        for (std::size_t index = 0; index < slots; ++index)
        {
            auto const value = std::isfinite(samples[index]) ? (std::max)(0.0, samples[index]) : 0.0;
            auto const share = peak > 0.0 ? (std::min)(1.0, value / peak) * ChartCeiling : 0.0;
            auto const lit = index >= slots - LitBars;
            bars.push_back(ChartBar{
                .Fraction = (std::max)(ChartFloor, share),
                .Opacity = lit ? 1.0 : 0.35 + (static_cast<double>(index) / static_cast<double>(slots)) * 0.45,
            });
        }
        return bars;
    }

    StorageFigures StorageFiguresOf(std::span<DownloadRecord const> records, std::optional<std::uint64_t> freeBytes) noexcept
    {
        StorageFigures figures;
        for (auto const& record : records)
        {
            if (record.Status == DownloadStatus::Done)
            {
                figures.StoredBytes += record.TotalBytes > 0 ? record.TotalBytes : record.DownloadedBytes;
            }
            else
            {
                figures.InFlightBytes += record.DownloadedBytes;
            }
        }
        figures.FreeBytes = freeBytes;
        auto const total = static_cast<double>(figures.StoredBytes)
            + static_cast<double>(figures.InFlightBytes)
            + static_cast<double>(freeBytes.value_or(0));
        if (total > 0.0)
        {
            figures.UsedFraction = (static_cast<double>(figures.StoredBytes) + static_cast<double>(figures.InFlightBytes)) / total;
            figures.StoredFraction = static_cast<double>(figures.StoredBytes) / total;
        }
        return figures;
    }

    std::wstring FreeLine(std::optional<std::uint64_t> freeBytes)
    {
        return freeBytes ? Logic::FormatDownloadBytes(static_cast<double>(*freeBytes)) + L" free" : std::wstring{ L"free space unknown" };
    }

    SplitSize SplitBytes(std::uint64_t bytes)
    {
        auto [value, unit] = SplitFormatted(Logic::FormatDownloadBytes(static_cast<double>(bytes)));
        return SplitSize{ .Value = value.empty() ? std::wstring{ L"0" } : std::move(value), .Unit = unit.empty() ? std::wstring{ L"B" } : std::move(unit) };
    }
}
