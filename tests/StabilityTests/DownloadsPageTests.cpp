#include "DownloadsPageTests.h"

#include "Services/DownloadStreamFacts.h"
#include "Services/Downloads/DownloadIndexStore.h"
#include "Services/Downloads/DownloadTypes.h"
#include "Services/DownloadsLogicPolicy.h"
#include "Storage/FileStorage.h"
#include "ViewModels/DownloadsPresentationPolicy.h"

#include <windows.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace
{
    namespace Logic = HaloDesktop::Services::DownloadsLogic;
    namespace Presentation = HaloDesktop::ViewModels::DownloadsPresentation;
    using HaloDesktop::Services::Downloads::DownloadFailureCode;
    using HaloDesktop::Services::Downloads::DownloadIndexStore;
    using HaloDesktop::Services::Downloads::DownloadMedia;
    using HaloDesktop::Services::Downloads::DownloadRecord;
    using HaloDesktop::Services::Downloads::DownloadStatus;
    using Presentation::DownloadRow;
    using Presentation::RowState;
    using Presentation::RowTone;

    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    constexpr std::uint64_t KiB = 1024ull;
    constexpr std::uint64_t MiB = 1024ull * 1024ull;
    constexpr std::uint64_t GiB = 1024ull * 1024ull * 1024ull;

    bool Near(double left, double right)
    {
        return std::abs(left - right) < 1e-9;
    }

    // `new Date(2026, 8, 5, 12).getTime()`: noon on 5 September 2026, local time.
    std::uint64_t LocalNoonSeptemberFifth()
    {
        std::tm local{};
        local.tm_year = 2026 - 1900;
        local.tm_mon = 8;
        local.tm_mday = 5;
        local.tm_hour = 12;
        local.tm_isdst = -1;
        auto const seconds = std::mktime(&local);
        if (seconds < 0) throw std::runtime_error{ "the downloads test could not build its creation time" };
        return static_cast<std::uint64_t>(seconds) * 1000ull;
    }

    // The reference's `view()`: a downloading episode, a quarter done at 12 MB/s.
    DownloadRecord View()
    {
        return DownloadRecord{
            .JobId = L"job",
            .AccountKey = std::wstring(64, L'a'),
            .Media = DownloadMedia{
                .VideoId = L"tt1:1:2",
                .ItemId = L"tt1",
                .MediaType = L"series",
                .MetaId = L"tt1",
                .Title = L"Cat’s in the Bag",
                .ShowName = L"Breaking Bad",
                .EpisodeLabel = L"S01E02",
                .Poster = L"https://img.test/poster.jpg",
                .StreamName = L"Torrentio 4K",
                .StreamTitle = L"Breaking.Bad.S01E02.2160p.HDR.HEVC",
            },
            .FileName = L"Breaking.Bad.S01E02-0123456789ab-fedcba98.mkv",
            .Status = DownloadStatus::Downloading,
            .TotalBytes = 4 * GiB,
            .DownloadedBytes = GiB,
            .SourceFingerprint = L"f",
            .CreatedAt = LocalNoonSeptemberFifth(),
            .BytesPerSecond = 12 * MiB,
        };
    }

    DownloadRecord With(DownloadRecord record, DownloadStatus status, std::optional<DownloadFailureCode> failure = std::nullopt)
    {
        record.Status = status;
        record.Failure = failure;
        return record;
    }

    DownloadRow RowOf(DownloadRecord const& record)
    {
        return Presentation::RowOf(record, HaloDesktop::Services::DownloadStreamFactsOf(record));
    }

    std::vector<std::wstring> Meta(std::initializer_list<wchar_t const*> parts)
    {
        std::vector<std::wstring> result;
        for (auto const part : parts) result.emplace_back(part);
        return result;
    }

    // ── downloadsLogic.test.ts ──────────────────────────────────────────────

    void TestStatusLabels()
    {
        Require(Logic::DownloadStatusLabel(DownloadStatus::Downloading, std::nullopt) == L"Downloading", "a downloading label was wrong");
        Require(Logic::DownloadStatusLabel(DownloadStatus::Done, std::nullopt) == L"Ready offline", "a ready label was wrong");
        Require(Logic::DownloadStatusLabel(DownloadStatus::Failed, DownloadFailureCode::SourceExpired) == L"Choose source again",
            "an expired source did not ask for a new source");
    }

    void TestProgressAndBytes()
    {
        Require(!Logic::DownloadProgress(0, 12).has_value(), "an unknown total had a progress");
        Require(Logic::DownloadProgress(100, 150) == 1.0, "progress was not bounded at one");
        Require(Logic::DownloadProgress(100, 25) == 0.25, "a quarter was not a quarter");
        Require(Logic::FormatDownloadBytes(0) == L"0 B", "zero bytes were not 0 B");
        Require(Logic::FormatDownloadBytes(1024) == L"1.0 KB", "a kilobyte was misprinted");
        Require(Logic::FormatDownloadBytes(1024.0 * 1024.0) == L"1.0 MB", "a megabyte was misprinted");
    }

    void TestFailureAndEta()
    {
        Require(Logic::DownloadFailureMessage(DownloadFailureCode::SourceExpired) == L"This source expired. Choose a source again to continue.",
            "the expired-source sentence changed");
        Require(!Logic::DownloadFailureMessage(std::nullopt).has_value(), "no failure had a sentence");
        Require(Logic::DownloadEtaSeconds(1000, 400, 100) == 6u, "time left was not six seconds");
        Require(!Logic::DownloadEtaSeconds(0, 0, 100).has_value(), "an unknown size had a time left");
        Require(Logic::FormatDownloadEta(3660) == L"1h 1m left", "an hour and a minute was misprinted");
        Require(Logic::FormatDownloadEta(45) == L"45s left", "seconds were misprinted");
        Require(Logic::FormatDownloadEta(256) == L"5m left", "minutes were not rounded up");
    }

    // The reference tests `nextDownloadedEpisode`; here the next offline episode
    // is picked by the service's `OfflineNext` on these positions.
    void TestEpisodeLabelsForOfflineContinuation()
    {
        using HaloDesktop::Services::Downloads::ParseEpisodePosition;
        auto const e99 = ParseEpisodePosition(std::wstring{ L"S01E99" });
        auto const e100 = ParseEpisodePosition(std::wstring{ L"s01.e100" });
        auto const e101 = ParseEpisodePosition(std::wstring{ L"S01 E101" });
        Require(e99 && e100 && e101, "a separated or three-digit episode label was not read");
        Require(*e99 < *e100 && *e100 < *e101, "episode positions did not order by number");
        Require(*ParseEpisodePosition(std::wstring{ L"S01E01" }) < *ParseEpisodePosition(std::wstring{ L"S01E02" }),
            "the second episode did not follow the first");
    }

    void TestOpaqueOwner()
    {
        using HaloDesktop::Services::Downloads::MakeAccountKey;
        auto const owner = MakeAccountKey(L"https://halo.example/", L"internal-user-id");
        Require(owner == MakeAccountKey(L"https://halo.example", L"internal-user-id"), "a trailing slash changed the download owner");
        Require(owner.size() == 64, "the download owner was not a SHA-256 digest");
        Require(owner.find(L"halo.example") == std::wstring::npos && owner.find(L"internal-user-id") == std::wstring::npos,
            "the download owner carried the server or user");
    }

    void TestRequiresNewSource()
    {
        Require(Logic::RequiresNewSource(DownloadFailureCode::MissingFile), "a missing file did not need a new source");
        Require(Logic::RequiresNewSource(DownloadFailureCode::SourceExpired), "an expired source did not need a new source");
        Require(!Logic::RequiresNewSource(DownloadFailureCode::Network), "a network failure needed a new source");
        Require(!Logic::RequiresNewSource(std::nullopt), "no failure needed a new source");
        Require(Logic::DownloadStatusLabel(DownloadStatus::Failed, DownloadFailureCode::MissingFile) == L"Choose source again",
            "a missing file did not ask for a new source");
    }

    void TestThroughputWindow()
    {
        std::vector<double> samples;
        for (std::size_t second = 1; second <= Logic::ThroughputSlots + 5; ++second)
        {
            Logic::PushThroughputSample(samples, static_cast<double>(second));
        }
        Require(samples.size() == Logic::ThroughputSlots, "the window kept more than its slots");
        Require(samples.front() == 6.0, "the window did not drop its oldest samples");
        Require(samples.back() == static_cast<double>(Logic::ThroughputSlots + 5), "the window lost its newest sample");
        std::vector<double> negative;
        Logic::PushThroughputSample(negative, -3.0);
        Require(negative.front() == 0.0, "a negative sample was stored");
        std::vector<double> notNumber;
        Logic::PushThroughputSample(notNumber, std::nan(""));
        Require(notNumber.front() == 0.0, "a non-finite sample was stored");
        Require(Logic::ThroughputSampleInterval.count() == 21'428'571, "one sample was not taken every 60000/28 ms");
    }

    std::vector<Logic::PausableRecord> Pausable(std::initializer_list<std::pair<wchar_t const*, DownloadStatus>> records)
    {
        std::vector<Logic::PausableRecord> result;
        for (auto const& [id, status] : records) result.push_back(Logic::PausableRecord{ .JobId = id, .Status = status });
        return result;
    }

    void TestBulkPause()
    {
        auto const records = Pausable({
            { L"running", DownloadStatus::Downloading },
            { L"waiting", DownloadStatus::Queued },
            { L"mine", DownloadStatus::Paused },
            { L"done", DownloadStatus::Done },
        });
        auto const plan = Logic::PlanPauseAll(records, {});
        Require(plan.Pause == std::vector<std::wstring>{ L"running", L"waiting" }, "pause all stopped the wrong transfers");
        Require(plan.Remembered == Logic::JobIdSet{ L"running", L"waiting" }, "pause all remembered the wrong transfers");
        auto const afterPause = Pausable({
            { L"running", DownloadStatus::Paused },
            { L"waiting", DownloadStatus::Paused },
            { L"mine", DownloadStatus::Paused },
            { L"done", DownloadStatus::Done },
        });
        Require(Logic::IsPausedAll(afterPause, plan.Remembered), "the page was not paused after pause all");
        Require(Logic::PlanResumeAll(afterPause, plan.Remembered) == std::vector<std::wstring>{ L"running", L"waiting" },
            "resume all restarted a transfer the viewer paused");

        auto const paused = Pausable({ { L"a", DownloadStatus::Paused } });
        Require(!Logic::IsPausedAll(paused, {}), "the page was paused with nothing bulk paused");
        Require(!Logic::IsPausedAll(Pausable({ { L"a", DownloadStatus::Paused }, { L"b", DownloadStatus::Queued } }), { L"a" }),
            "the page was paused while something was queued");
        Require(!Logic::IsPausedAll(Pausable({ { L"a", DownloadStatus::Downloading } }), { L"a" }),
            "the page was paused while the remembered transfer ran");
        Require(!Logic::IsPausedAll({}, { L"gone" }), "the page was paused with nothing left");

        auto const second = Logic::PlanPauseAll(
            Pausable({ { L"earlier", DownloadStatus::Paused }, { L"new", DownloadStatus::Downloading } }),
            { L"earlier" });
        Require(second.Pause == std::vector<std::wstring>{ L"new" }, "a second pause all stopped the wrong transfers");
        Require(second.Remembered == Logic::JobIdSet{ L"earlier", L"new" }, "a second pause all forgot the first");
    }

    void TestLinePeak()
    {
        Require(Logic::IsNewLinePeak(100, 0), "a first reading was not a peak");
        Require(!Logic::IsNewLinePeak(105, 100), "a small gain was a peak");
        Require(Logic::IsNewLinePeak(111, 100), "a clear gain was not a peak");
        Require(!Logic::IsNewLinePeak(0, 0), "nothing was a peak");
        Require(!Logic::IsNewLinePeak(INFINITY, 0), "an infinite reading was a peak");
    }

    struct Listed final
    {
        std::wstring JobId;
        std::uint64_t CreatedAt{};
        std::uint64_t DownloadedBytes{};

        bool operator==(Listed const&) const = default;
    };

    void TestMergeListRead()
    {
        // The list was read at event 4; jobs b (progress), c (removed) and d (new) changed after it.
        std::vector<Listed> const listed{ { L"a", 1, 10 }, { L"b", 2, 50 }, { L"c", 3, 0 } };
        std::vector<Listed> const current{ { L"a", 1, 5 }, { L"b", 2, 80 }, { L"d", 4, 0 } };
        std::unordered_map<std::wstring, std::uint64_t> const events{ { L"a", 3 }, { L"b", 6 }, { L"c", 7 }, { L"d", 8 } };
        Require(Logic::MergeListRead(listed, current, events, 4) == std::vector<Listed>{ { L"a", 1, 10 }, { L"b", 2, 80 }, { L"d", 4, 0 } },
            "a late list read undid newer engine events");
        Require(Logic::MergeListRead(std::vector<Listed>{ { L"b", 2, 0 }, { L"a", 1, 0 } }, std::vector<Listed>{}, {}, 0)
                == std::vector<Listed>{ { L"a", 1, 0 }, { L"b", 2, 0 } },
            "a list read with no newer events was not taken as is, by creation");
    }

    // The same rule over the service's own record type, which is what it merges.
    void TestMergeListReadOverRecords()
    {
        auto record = [](wchar_t const* id, std::uint64_t created, std::uint64_t bytes)
        {
            auto value = View();
            value.JobId = id;
            value.CreatedAt = created;
            value.DownloadedBytes = bytes;
            return value;
        };
        auto const merged = Logic::MergeListRead(
            std::vector<DownloadRecord>{ record(L"a", 1, 10), record(L"b", 2, 50), record(L"c", 3, 0) },
            std::vector<DownloadRecord>{ record(L"a", 1, 5), record(L"b", 2, 80), record(L"d", 4, 0) },
            { { L"a", 3 }, { L"b", 6 }, { L"c", 7 }, { L"d", 8 } },
            4);
        Require(merged.size() == 3
                && merged[0].JobId == L"a" && merged[0].DownloadedBytes == 10
                && merged[1].JobId == L"b" && merged[1].DownloadedBytes == 80
                && merged[2].JobId == L"d",
            "the service's merge of a late list read undid newer engine events");
    }

    // ── downloadsPresentation.test.ts ───────────────────────────────────────

    void TestDownloadingEpisodeRow()
    {
        auto const row = RowOf(View());
        Require(row.Tag == L"S01E02", "the episode tag was wrong");
        Require(row.Name == L"Breaking Bad", "the row was not named for the show");
        Require(row.Sub == L"Cat’s in the Bag", "the row's sub line was not the episode title");
        Require(row.Lead == L"12.0 MB/s", "the lead line was not the rate");
        Require(row.Tone == RowTone::Normal, "a downloading row was not normal");
        Require(row.DownloadedLine == L"1.0 GB / 4.0 GB", "the downloaded line was wrong");
        Require(row.Progress == 0.25, "the progress was not a quarter");
        Require(row.QualityMedal == HaloDesktop::ViewModels::Medal{ L"4K", L"HDR", HaloDesktop::ViewModels::MedalFinish::Gold },
            "the medal was not 4K HDR gold");
        Require(row.SubsChip == L"NO SUBS" && row.SubsMuted && row.Subs == L"None", "a row without subtitles said otherwise");
        Require(row.TransferMeta == Meta({ L"1.0 of 4.0 GB", L"25%", L"5m left" }), "the queue line was wrong");
        Require(row.AddedLabel == L"ADDED 05 SEP", "the added label was wrong");
        Require(Presentation::AddedFact(row.AddedLabel) == L"05 SEP", "the added fact kept its prefix");
        Require(row.CanPause && !row.CanResume, "a downloading row's actions were wrong");
    }

    void TestMovieRow()
    {
        auto record = View();
        record.FileName = L"Heat";
        record.Media.MediaType = L"movie";
        record.Media.ShowName.reset();
        record.Media.EpisodeLabel.reset();
        record.Media.Title = L"Heat";
        auto const row = RowOf(record);
        Require(row.Tag == L"MOVIE", "a film was not tagged MOVIE");
        Require(row.Name == L"Heat" && row.Sub.empty(), "a film repeated its name");
        Require(row.EpisodeLabel.empty(), "a film had an episode label");
        Require(row.CardTitle == L"Heat" && row.CardSub.empty(), "a film's card was wrong");
        Require(row.PaneMeta == L"MOVIE", "a film's pane meta was wrong");
    }

    void TestReadyEpisodeCard()
    {
        auto record = With(View(), DownloadStatus::Done);
        record.DownloadedBytes = 4 * GiB;
        record.SubtitleLanguage = L"eng";
        auto const row = RowOf(record);
        Require(row.CardTitle == L"Breaking Bad" && row.CardSub == L"Cat’s in the Bag", "the card did not name the show and episode");
        Require(row.EpisodeLabel == L"S01E02", "the card lost its episode badge");
        Require(row.PaneMeta == L"S01E02 · Cat’s in the Bag", "the pane meta was wrong");
        Require(row.CardMeta == L"HEVC · 4.0 GB · ENG", "the card meta was wrong");
        Require(row.Badge == Presentation::QualityBadge{ .Label = L"4K HDR", .Gold = true }, "the card's badge was not gold 4K HDR");
        Require(row.Subs == L"English", "the subtitle fact did not name the language");
    }

    void TestCardLeavesOutUnstated()
    {
        auto record = With(View(), DownloadStatus::Done);
        record.FileName = L"video.mkv";
        record.Media.StreamName = L"Addon";
        record.Media.StreamTitle = L"Some upload";
        auto const row = RowOf(record);
        Require(!row.Badge.has_value(), "a card invented a quality");
        Require(row.CardMeta == L"4.0 GB · NO SUBS", "a card printed what the name did not state");
    }

    void TestQueueLine()
    {
        Require(RowOf(With(View(), DownloadStatus::Paused)).TransferMeta == Meta({ L"1.0 of 4.0 GB", L"25%", L"Paused by you" }),
            "a paused row did not say so");
        auto waiting = View();
        waiting.BytesPerSecond = 0;
        Require(RowOf(waiting).TransferMeta == Meta({ L"1.0 of 4.0 GB", L"25%", L"Waiting for the source" }),
            "a stalled row did not say it waits for the source");
        Require(RowOf(With(View(), DownloadStatus::Failed, DownloadFailureCode::Network)).TransferMeta
                == Meta({ L"The download could not continue after repeated network failures." }),
            "a failure's sentence did not stand alone");
        auto unknown = View();
        unknown.TotalBytes = 0;
        unknown.Media.VideoSize.reset();
        Require(RowOf(unknown).TransferMeta == Meta({ L"1.0 GB so far" }), "an unknown size did not say what arrived");
        Require(Presentation::BytesOfTotal(512 * MiB, 4 * GiB) == L"512.0 MB of 4.0 GB", "sizes in two units were misprinted");
    }

    void TestLeadAndTone()
    {
        auto waiting = View();
        waiting.BytesPerSecond = 0;
        Require(RowOf(waiting).Lead == L"WAITING", "a stalled lead was wrong");
        Require(RowOf(With(View(), DownloadStatus::Queued)).Lead == L"WAITING FOR THE CURRENT TRANSFER", "a queued lead was wrong");
        auto const paused = RowOf(With(View(), DownloadStatus::Paused));
        Require(paused.Lead == L"PAUSED BY YOU" && paused.Tone == RowTone::Caution, "a paused lead or tone was wrong");
        auto const failed = RowOf(With(View(), DownloadStatus::Failed, DownloadFailureCode::Network));
        Require(failed.Lead == L"The download could not continue after repeated network failures." && failed.Tone == RowTone::Critical,
            "a failed lead or tone was wrong");
        Require(RowOf(With(View(), DownloadStatus::Done)).Lead == L"READY FOR OFFLINE PLAYBACK", "a ready lead was wrong");
    }

    // The sentences the lead line prints: nine as the reference words them,
    // and this app's own for a source that sent a placeholder clip.
    void TestFailureSentences()
    {
        std::array<std::pair<DownloadFailureCode, wchar_t const*>, 10> const sentences{ {
            { DownloadFailureCode::SourceNotReady, L"This source is not ready yet: it sent a short placeholder instead of the video. Try again later or choose another source." },
            { DownloadFailureCode::SourceExpired, L"This source has expired. Choose a source again to continue." },
            { DownloadFailureCode::StorageFull, L"The device ran out of storage while downloading." },
            { DownloadFailureCode::InvalidRange, L"The source could not safely resume this download." },
            { DownloadFailureCode::MissingFile, L"This download is no longer on the device." },
            { DownloadFailureCode::Network, L"The download could not continue after repeated network failures." },
            { DownloadFailureCode::ServerUnavailable, L"The source is still unavailable after repeated retries." },
            { DownloadFailureCode::SourceRejected, L"The source refused this download." },
            { DownloadFailureCode::ProtectedRequestCorrupt, L"The protected download request could not be read. Choose the source again." },
            { DownloadFailureCode::Unknown, L"This download could not be completed." },
        } };
        for (auto const& [code, sentence] : sentences)
        {
            Require(RowOf(With(View(), DownloadStatus::Failed, code)).Lead == sentence, "a failure sentence was worded differently");
        }
        Require(RowOf(With(View(), DownloadStatus::Failed)).Lead == L"This download could not be completed.",
            "a failure without a code did not read as unknown");
    }

    void TestFailureActions()
    {
        auto const retryable = RowOf(With(View(), DownloadStatus::Failed, DownloadFailureCode::ServerUnavailable));
        Require(retryable.CanResume && retryable.CanRetry && !retryable.RequiresNewSource, "a retryable failure's actions were wrong");
        auto const expired = RowOf(With(View(), DownloadStatus::Failed, DownloadFailureCode::MissingFile));
        Require(!expired.CanResume && !expired.CanRetry && expired.RequiresNewSource && !expired.CanPause,
            "an expired failure offered more than a new source");
    }

    void TestFinishedDownload()
    {
        auto record = With(View(), DownloadStatus::Done);
        record.DownloadedBytes = 4 * GiB;
        record.SubtitleLanguage = L"en";
        auto const row = RowOf(record);
        Require(row.State == RowState::OnDisk, "a finished download was not on disk");
        Require(row.DownloadedLine == L"4.0 GB" && row.Progress == 1.0, "a finished download showed two sizes or partial progress");
        Require(row.SubsChip == L"SUB EN" && row.Subs == L"en", "a finished download's subtitle was wrong");
    }

    void TestUnknownSize()
    {
        auto record = View();
        record.TotalBytes = 0;
        record.Media.VideoSize.reset();
        auto const row = RowOf(record);
        Require(row.DownloadedLine == L"SIZE UNKNOWN · 1.0 GB" && row.Progress == 0.0, "an unknown size was not said");
        Require(RowOf(With(record, DownloadStatus::Done)).DownloadedLine == L"SIZE UNKNOWN", "a finished unknown size was wrong");
    }

    void TestRowArtwork()
    {
        Require(RowOf(View()).RowArtwork == L"https://img.test/poster.jpg", "a row without a still lost its poster");
        auto record = View();
        record.Media.LandscapeArtwork = L"https://img.test/still.jpg";
        Require(RowOf(record).RowArtwork == L"https://img.test/still.jpg", "a row ignored its landscape still");
    }

    void TestFiltersAndCounts()
    {
        auto a = View();
        a.JobId = L"a";
        auto b = With(View(), DownloadStatus::Failed, DownloadFailureCode::Network);
        b.JobId = L"b";
        auto c = With(View(), DownloadStatus::Done);
        c.JobId = L"c";
        std::vector<DownloadRow> const rows{ RowOf(a), RowOf(b), RowOf(c) };
        std::vector<DownloadRow> const transfers{ rows[0], rows[1] };
        std::vector<DownloadRow> const ready{ rows[2] };
        Require(Presentation::CountFilters(transfers, ready) == Presentation::FilterCounts{ .All = 3, .Active = 2, .Ready = 1, .Failed = 1 },
            "the filter counts were wrong");
        Require(Presentation::MatchesFilter(rows[1], Presentation::Filter::Active), "Active left out a failed transfer");
        Require(!Presentation::MatchesFilter(rows[2], Presentation::Filter::Active), "Active showed a finished download");
        Require(Presentation::NoMatchesLine(Presentation::Filter::Active) == L"No active transfers right now."
                && Presentation::NoMatchesLine(Presentation::Filter::Ready) == L"No downloads are ready to watch yet."
                && Presentation::NoMatchesLine(Presentation::Filter::Failed) == L"No failed transfers need attention."
                && Presentation::NoMatchesLine(Presentation::Filter::All) == L"No downloads match this filter right now.",
            "a no-match line was worded differently");
    }

    void TestSelection()
    {
        auto transferRecord = View();
        transferRecord.JobId = L"t";
        auto readyRecord = With(View(), DownloadStatus::Done);
        readyRecord.JobId = L"r";
        std::vector<DownloadRow> const transfers{ RowOf(transferRecord) };
        std::vector<DownloadRow> const ready{ RowOf(readyRecord) };
        Require(Presentation::ResolveSelection(L"t", ready, transfers)->Id == L"t", "a visible pick was not kept");
        Require(Presentation::ResolveSelection(L"gone", ready, transfers)->Id == L"r", "a hidden pick did not fall back to the first ready download");
        Require(Presentation::ResolveSelection(L"", {}, transfers)->Id == L"t", "no pick did not fall back to the first transfer");
        Require(Presentation::ResolveSelection(L"", {}, {}) == nullptr, "nothing at all was selected");
    }

    void TestRate()
    {
        Require(Presentation::RateText(12.345, false) == L"12.3 MB/s", "the rate was misprinted");
        Require(Presentation::RateText(12.345, true) == L"PAUSED", "a bulk pause did not read PAUSED");
        Require(Presentation::RateFigureOf(0, false) == Presentation::RateFigure{ .Value = L"0.0", .Unit = L"MB/s" }, "an idle figure was wrong");
        Require(Presentation::RateFigureOf(3, true) == Presentation::RateFigure{ .Value = L"PAUSED", .Unit = L"" }, "a paused figure had a unit");
        Require(Presentation::RateToneOf(0, false) == Presentation::RateTone::Idle, "zero was not idle");
        Require(Presentation::RateToneOf(1, false) == Presentation::RateTone::Normal, "moving was not normal");
        Require(Presentation::RateToneOf(1, true) == Presentation::RateTone::Paused, "a bulk pause was not paused");
        Require(Presentation::QueueLine(0) == L"No active transfers"
                && Presentation::QueueLine(1) == L"1 active transfer"
                && Presentation::QueueLine(3) == L"3 active transfers",
            "the queue line was worded differently");
    }

    void TestChart()
    {
        std::vector<double> const samples{ 1, 2, 4 };
        auto const bars = Presentation::ChartBars(samples);
        Require(bars.size() == 28, "the chart did not have 28 slots");
        for (std::size_t index = 0; index < 25; ++index)
        {
            Require(bars[index].Fraction == Presentation::ChartFloor, "an empty slot was not on the floor");
        }
        Require(bars[27].Fraction == Presentation::ChartCeiling, "the peak did not reach the ceiling");
        Require(bars[26].Fraction == 0.5 * Presentation::ChartCeiling, "half the peak was not half the ceiling");
        Require(bars[25].Fraction == 0.25 * Presentation::ChartCeiling, "a quarter of the peak was not a quarter of the ceiling");
        for (std::size_t index = 24; index < 28; ++index)
        {
            Require(bars[index].Opacity == 1.0, "one of the newest four bars was not lit");
        }
        Require(bars[0].Opacity == 0.35, "the oldest bar's opacity was wrong");
        Require(Near(bars[23].Opacity, 0.35 + (23.0 / 28.0) * 0.45), "an older bar did not recede");
        for (auto const& bar : Presentation::ChartBars({}))
        {
            Require(bar.Fraction == Presentation::ChartFloor, "an idle minute left the floor");
        }
        std::vector<double> history(30, 0.0);
        history.push_back(8.0);
        history.insert(history.end(), 27, 2.0);
        auto const longer = Presentation::ChartBars(history);
        Require(longer[0].Fraction == Presentation::ChartCeiling, "a longer history did not keep its newest slots");
        Require(longer[27].Fraction == 0.25 * Presentation::ChartCeiling, "a longer history drew against the wrong peak");
        std::vector<double> const peaks{ 0.4, 3.25, 1 };
        Require(Presentation::PeakText(peaks) == L"PEAK 3.3 MB/S", "the peak was misprinted");
        Require(Presentation::PeakText({}) == L"PEAK 0.0 MB/S", "an empty peak was misprinted");
    }

    void TestSummary()
    {
        std::vector<DownloadRow> rows;
        auto add = [&rows](wchar_t const* id, DownloadStatus status)
        {
            auto record = With(View(), status);
            record.JobId = id;
            rows.push_back(RowOf(record));
        };
        add(L"a", DownloadStatus::Downloading);
        add(L"b", DownloadStatus::Queued);
        add(L"c", DownloadStatus::Done);
        add(L"d", DownloadStatus::Done);
        add(L"e", DownloadStatus::Done);
        Require(Presentation::SummaryLine(rows) == L"1 downloading · 1 waiting · 3 ready", "the summary was wrong");
        std::vector<DownloadRow> const failed{ RowOf(With(View(), DownloadStatus::Failed, DownloadFailureCode::Network)) };
        Require(Presentation::SummaryLine(failed) == L"1 failed", "a lone failure's summary was wrong");
        Require(Presentation::SummaryLine({}).empty(), "nothing had a summary");
        Require(Presentation::ReadyHeadline(3, static_cast<std::uint64_t>(53.4 * static_cast<double>(GiB))) == L"3 · 53.4 GB",
            "the ready headline was wrong");
    }

    void TestConnections()
    {
        auto a = View();
        a.JobId = L"a";
        a.ActiveConnections = 5;
        auto b = With(View(), DownloadStatus::Paused);
        b.JobId = L"b";
        b.ActiveConnections = 3;
        auto c = With(View(), DownloadStatus::Done);
        c.JobId = L"c";
        std::vector<DownloadRecord> const items{ a, b, c };
        Require(Presentation::TotalConnections(items) == 5, "connections were summed over more than running transfers");
        Require(Presentation::ConnectionsLabel(1) == L"1 connection" && Presentation::ConnectionsLabel(6) == L"6 connections",
            "the connections label was worded differently");
        Require(Presentation::RateNoteOf(items) == Presentation::RateNote{ .Kind = Presentation::RateNoteKind::Connections, .Text = L"5 connections" },
            "the pill did not carry the connections");
        std::vector<DownloadRecord> const connecting{ View() };
        Require(Presentation::RateNoteOf(connecting) == Presentation::RateNote{ .Kind = Presentation::RateNoteKind::Note, .Text = L"Connecting" },
            "a transfer opening its first connection claimed zero");
        std::vector<DownloadRecord> const paused{ With(View(), DownloadStatus::Paused) };
        Require(Presentation::RateNoteOf(paused) == Presentation::RateNote{ .Kind = Presentation::RateNoteKind::Note, .Text = L"No active transfers" },
            "a paused queue's note was wrong");
        std::vector<DownloadRecord> const done{ With(View(), DownloadStatus::Done) };
        Require(Presentation::RateNoteOf(done) == Presentation::RateNote{ .Kind = Presentation::RateNoteKind::Note, .Text = L"Idle" },
            "nothing to transfer was not idle");
        Require(Presentation::RateNoteOf({}) == Presentation::RateNote{ .Kind = Presentation::RateNoteKind::Note, .Text = L"Idle" },
            "an empty page was not idle");
    }

    void TestStorage()
    {
        auto stored = With(View(), DownloadStatus::Done);
        stored.TotalBytes = 600;
        stored.DownloadedBytes = 600;
        auto flight = With(View(), DownloadStatus::Failed);
        flight.TotalBytes = 1000;
        flight.DownloadedBytes = 200;
        std::vector<DownloadRecord> const items{ stored, flight };
        auto const figures = Presentation::StorageFiguresOf(items, 200);
        Require(figures.StoredBytes == 600 && figures.InFlightBytes == 200, "storage counted the wrong bytes");
        Require(figures.UsedFraction == 0.8 && figures.StoredFraction == 0.6, "storage shares were wrong");
        Require(Presentation::FreeLine(std::nullopt) == L"free space unknown", "unknown free space was worded differently");
        Require(Presentation::FreeLine(GiB) == L"1.0 GB free", "free space was misprinted");
        Require(Presentation::SplitBytes(static_cast<std::uint64_t>(55.5 * static_cast<double>(GiB))) == Presentation::SplitSize{ .Value = L"55.5", .Unit = L"GB" },
            "a size did not split into figure and unit");
        Require(Presentation::SplitBytes(0) == Presentation::SplitSize{ .Value = L"0", .Unit = L"B" }, "zero did not split");
        Require(Presentation::AddedLabel(0) == L"ADDED UNKNOWN", "an unknown creation time had a date");
        Require(Logic::FormatDownloadBytes(static_cast<double>(KiB) - 1.0) == L"1023 B", "bytes below a kilobyte were not whole");
    }

    // ── The dynamic range a download keeps ──────────────────────────────────

    void TestDolbyVisionBadge()
    {
        auto parsed = With(View(), DownloadStatus::Done);
        parsed.Media.StreamTitle = L"Breaking.Bad.S01E02.2160p.DV.HEVC";
        Require(RowOf(parsed).Badge == Presentation::QualityBadge{ .Label = L"4K DV", .Gold = true }, "a Dolby Vision name read as HDR");

        // The sheet saw DV in the stream's description, which the download does not keep.
        auto stored = With(View(), DownloadStatus::Done);
        stored.Media.StreamTitle = L"Breaking.Bad.S01E02.2160p.HEVC";
        stored.Media.DynamicRange = L"DV";
        Require(RowOf(stored).Badge == Presentation::QualityBadge{ .Label = L"4K DV", .Gold = true }, "a stored Dolby Vision range was not used");

        // A record written before the range was kept reads its name, as before.
        auto old = With(View(), DownloadStatus::Done);
        Require(!old.Media.DynamicRange.has_value(), "the test record already had a range");
        Require(RowOf(old).Badge == Presentation::QualityBadge{ .Label = L"4K HDR", .Gold = true }, "an old record lost its HDR badge");
    }

    class TemporaryDirectory final
    {
    public:
        TemporaryDirectory()
        {
            GUID id{};
            winrt::check_hresult(CoCreateGuid(&id));
            std::array<wchar_t, 40> text{};
            if (StringFromGUID2(id, text.data(), static_cast<int>(text.size())) == 0)
            {
                throw std::runtime_error{ "the downloads test could not name its temporary folder" };
            }
            m_path = std::filesystem::temp_directory_path() / (std::wstring{ L"HaloDesktop-DownloadsPage-" } + text.data());
            std::filesystem::create_directories(m_path);
        }
        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }
        TemporaryDirectory(TemporaryDirectory const&) = delete;
        TemporaryDirectory& operator=(TemporaryDirectory const&) = delete;
        [[nodiscard]] std::filesystem::path const& Path() const noexcept { return m_path; }

    private:
        std::filesystem::path m_path;
    };

    DownloadRecord IndexRecord(std::wstring jobId)
    {
        auto record = With(View(), DownloadStatus::Paused);
        record.JobId = std::move(jobId);
        record.RootPath = std::filesystem::temp_directory_path();
        record.SourceFingerprint = std::wstring(64, L'b');
        record.ExplicitPause = true;
        record.BytesPerSecond = 0;
        record.CreatedAt = 1;
        record.UpdatedAt = 1;
        return record;
    }

    void TestDynamicRangeIndexRoundTrip()
    {
        TemporaryDirectory temporary;
        auto const dataRoot = temporary.Path() / L"state";
        {
            DownloadIndexStore store{ dataRoot };
            auto withRange = IndexRecord(L"job-dv");
            withRange.Media.DynamicRange = L"DV";
            store.Save({ withRange, IndexRecord(L"job-none") }, 1);
        }
        DownloadIndexStore reopened{ dataRoot };
        auto const records = reopened.Load(false);
        Require(records.size() == 2, "the index did not keep both records");
        for (auto const& record : records)
        {
            if (record.JobId == L"job-dv")
            {
                Require(record.Media.DynamicRange == std::optional<std::wstring>{ L"DV" }, "the dynamic range did not survive the index");
            }
            else
            {
                Require(!record.Media.DynamicRange.has_value(), "a record without a range gained one");
            }
        }
    }

    // An index written by a build that did not keep the range has no such key at
    // all; it loads unchanged, with the range left empty.
    void TestIndexWithoutDynamicRangeLoads()
    {
        TemporaryDirectory temporary;
        auto const dataRoot = temporary.Path() / L"state";
        {
            DownloadIndexStore store{ dataRoot };
            auto record = IndexRecord(L"job-old");
            record.Media.DynamicRange = L"HDR10";
            store.Save({ record }, 1);
        }
        auto const indexPath = dataRoot / L"downloads-index.json";
        auto root = winrt::Windows::Data::Json::JsonObject::Parse(
            winrt::to_hstring(HaloDesktop::Storage::ReadUtf8File(indexPath, 32u * 1024u * 1024u)));
        auto const entries = root.GetNamedArray(L"entries");
        auto const media = entries.GetObjectAt(0).GetNamedObject(L"media");
        Require(media.HasKey(L"dynamicRange"), "the index did not write the dynamic range");
        media.Remove(L"dynamicRange");
        HaloDesktop::Storage::WriteUtf8FileAtomic(indexPath, winrt::to_string(root.Stringify()));

        DownloadIndexStore reopened{ dataRoot };
        auto const records = reopened.Load(false);
        Require(records.size() == 1 && records.front().JobId == L"job-old", "an index without the range did not load");
        Require(!records.front().Media.DynamicRange.has_value(), "an index without the range invented one");
        Require(records.front().Media.StreamTitle == std::optional<std::wstring>{ L"Breaking.Bad.S01E02.2160p.HDR.HEVC" },
            "an index without the range lost its stream title");
        Require(RowOf(With(records.front(), DownloadStatus::Done)).Badge == Presentation::QualityBadge{ .Label = L"4K HDR", .Gold = true },
            "an old record did not fall back to the name's range");
    }
}

void RunDownloadsPageTests()
{
    TestStatusLabels();
    TestProgressAndBytes();
    TestFailureAndEta();
    TestEpisodeLabelsForOfflineContinuation();
    TestOpaqueOwner();
    TestRequiresNewSource();
    TestThroughputWindow();
    TestBulkPause();
    TestLinePeak();
    TestMergeListRead();
    TestMergeListReadOverRecords();
    TestDownloadingEpisodeRow();
    TestMovieRow();
    TestReadyEpisodeCard();
    TestCardLeavesOutUnstated();
    TestQueueLine();
    TestLeadAndTone();
    TestFailureSentences();
    TestFailureActions();
    TestFinishedDownload();
    TestUnknownSize();
    TestRowArtwork();
    TestFiltersAndCounts();
    TestSelection();
    TestRate();
    TestChart();
    TestSummary();
    TestConnections();
    TestStorage();
    TestDolbyVisionBadge();
    TestDynamicRangeIndexRoundTrip();
    TestIndexWithoutDynamicRangeLoads();
}
