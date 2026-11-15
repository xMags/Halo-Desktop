#include "SourcesSheetTests.h"

#include "Api/Dto.h"
#include "Services/AddonNamePolicy.h"
#include "Services/DownloadSourceMatch.h"
#include "Services/Downloads/DownloadTypes.h"
#include "Services/StreamInfo.h"
#include "ViewModels/QualityMedalPolicy.h"
#include "ViewModels/SourcePresentation.h"
#include "ViewModels/SourcesModel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    void Require(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error{ message };
        }
    }

    namespace Sources = HaloDesktop::Sources;
    using HaloDesktop::Api::Dto::AddonFailure;
    using HaloDesktop::Api::Dto::StreamGroup;
    using HaloDesktop::Api::Dto::StreamRecord;
    using HaloDesktop::Api::Dto::StreamsPayload;
    using HaloDesktop::Services::Downloads::DownloadStatus;
    using HaloDesktop::Services::Downloads::VideoDownload;
    using HaloDesktop::ViewModels::Medal;
    using HaloDesktop::ViewModels::MedalFinish;
    using SaveKind = HaloDesktop::Sources::SaveState::Kind;

    constexpr std::uint64_t GiB = 1024ull * 1024ull * 1024ull;
    constexpr std::uint64_t MiB = 1024ull * 1024ull;

    // The reference's `stream(title, extra)`: an address plus the title.
    StreamRecord Stream(std::wstring const& title)
    {
        StreamRecord stream;
        stream.Url = winrt::hstring{ L"https://cdn.test/" + std::to_wstring(std::hash<std::wstring>{}(title)) };
        stream.Title = winrt::hstring{ title };
        return stream;
    }

    StreamRecord WithFilename(StreamRecord stream, std::wstring const& filename)
    {
        stream.Filename = winrt::hstring{ filename };
        return stream;
    }

    StreamGroup Group(std::wstring const& id, std::wstring const& name, std::vector<StreamRecord> streams)
    {
        return StreamGroup{ winrt::hstring{ id }, winrt::hstring{ name }, std::move(streams) };
    }

    // The reference keys a source `addon:index`; the app mints its own, which the
    // model never interprets.
    Sources::Resolve Resolve(
        std::vector<StreamGroup> groups,
        std::vector<AddonFailure> errors = {},
        double durationSeconds = 0.0)
    {
        StreamsPayload payload{ std::move(groups), std::move(errors) };
        auto const parsed = Sources::ParseResolve(payload, [&payload](std::size_t group, std::size_t index)
        {
            return payload.Results[group].AddonId + L":" + winrt::to_hstring(index);
        });
        return Sources::ResolveSources(parsed, durationSeconds);
    }

    VideoDownload Download()
    {
        return VideoDownload{
            .JobId = L"job1",
            .ReleaseName = L"Show.S01E01.2160p.HEVC.mkv",
            .Status = DownloadStatus::Done,
            .SizeBytes = 13 * GiB,
            .Fraction = 1.0,
        };
    }

    Sources::ListingOptions Options(
        Sources::SourceFilter filter = Sources::SourceFilter::All,
        Sources::SourceSort sort = Sources::SourceSort::Recommended,
        bool coldRevealed = false,
        Sources::DeviceContext device = {})
    {
        return Sources::ListingOptions{ .Filter = filter, .Sort = sort, .ColdRevealed = coldRevealed, .Device = device };
    }

    std::vector<std::wstring> Keys(std::vector<Sources::SourceEntry> const& pool)
    {
        std::vector<std::wstring> keys;
        for (auto const& entry : pool) keys.emplace_back(entry.Key.c_str());
        return keys;
    }

    std::vector<std::wstring> Stops(Sources::Listing const& listing)
    {
        std::vector<std::wstring> keys;
        for (auto const& key : Sources::SelectionStops(listing)) keys.emplace_back(key.c_str());
        return keys;
    }

    bool SameState(Sources::SaveState const& state, SaveKind kind, bool replaces = false, std::optional<double> fraction = std::nullopt)
    {
        return state == Sources::SaveState{ kind, replaces, fraction };
    }

    std::optional<winrt::hstring> Quality(std::wstring const& title)
    {
        return HaloDesktop::Services::ParseStreamInfo(Stream(title)).Quality;
    }

    std::optional<winrt::hstring> Audio(std::wstring const& title)
    {
        return HaloDesktop::Services::ParseStreamInfo(Stream(title)).Audio;
    }

    std::optional<std::uint64_t> Size(std::wstring const& title, std::optional<std::uint64_t> videoSize = std::nullopt)
    {
        auto stream = Stream(title);
        stream.VideoSize = videoSize;
        return HaloDesktop::Services::ParseStreamInfo(stream).SizeBytes;
    }

    void TestSizesAndStatus()
    {
        using HaloDesktop::Services::FormatStreamSize;
        Require(FormatStreamSize(static_cast<std::uint64_t>(8.42 * static_cast<double>(GiB))) == L"8.42 GB", "a size under ten gigabytes did not print two decimals");
        Require(FormatStreamSize(static_cast<std::uint64_t>(34.2 * static_cast<double>(GiB))) == L"34.2 GB", "a size over ten gigabytes did not print one decimal");
        Require(FormatStreamSize(std::nullopt) == L"UNKNOWN", "an unknown size was not UNKNOWN");

        Require(Sources::StatusLabel(Sources::SourceStatus::Instant) == L"Plays instantly", "the instant label changed");
        Require(Sources::StatusLabel(Sources::SourceStatus::Unknown) == L"May download before it plays", "an unreported cache did not read as a possible wait");
        Require(Sources::StatusLabel(Sources::SourceStatus::Uncached) == L"Downloads before it plays", "the uncached label changed");
    }

    void TestAddonNames()
    {
        using HaloDesktop::Services::SanitizeAddonName;
        Require(SanitizeAddonName(winrt::hstring{ L"Torrentio" }) == L"Torrentio", "a plain addon name was changed");
        Require(SanitizeAddonName(winrt::hstring{ L"https://evil.test/manifest.json" }) == L"An addon", "a URL was printed as an addon name");
        Require(SanitizeAddonName(winrt::hstring{ std::wstring(40, L'a') }) == L"An addon", "a token was printed as an addon name");
        Require(SanitizeAddonName(std::nullopt) == L"An addon", "a missing addon name was not withheld");
        Require(SanitizeAddonName(winrt::hstring{ L"  Comet\x0007 " }) == L"Comet", "control characters and spaces were not dropped");
    }

    void TestSavedDownloadStaysOutOfTheList()
    {
        auto const result = Resolve({ Group(L"a", L"Addon", {
            WithFilename(Stream(L"Show.S01E01.2160p.HEVC\n\x26A1 Cached"), L"Show.S01E01.2160p.HEVC.mkv"),
            Stream(L"Show.S01E01.1080p\n4.1 GB"),
        }) });
        Require(Keys(result.Pool) == std::vector<std::wstring>{ L"a:0", L"a:1" }, "a saved download changed the ranked list");
        Require(result.Summary == L"2 sources \x00B7 1 addon", "the count line counted something other than addon sources");
        Require(result.Providers.size() == 1 && result.Providers[0].Name == L"Addon" && result.Providers[0].Value == L"2 sources",
            "the footer's provider column was not the one addon");
        Require(Sources::CountFilters(result.Pool) == Sources::FilterCounts{ 2, 1, 1, 0 }, "the filter counts were wrong");

        auto const listing = Sources::BuildListing(result, Options());
        Require(listing.Best && listing.Best->Entry.Key == L"a:0", "the pick was not the top-ranked addon source");

        auto const saved = Download();
        Require(SameState(Sources::SaveStateFor(result.Find(L"a:0"), &saved, false), SaveKind::Saved),
            "the source the download came from was not marked saved");
        Require(SameState(Sources::SaveStateFor(result.Find(L"a:1"), &saved, false), SaveKind::Ready, true),
            "another source did not offer to save instead");
        Require(SameState(Sources::SaveStateFor(result.Find(L"a:1"), nullptr, false), SaveKind::Ready, false),
            "a source with no download did not offer to save");
    }

    void TestSaveStates()
    {
        auto const result = Resolve({ Group(L"a", L"Addon", { WithFilename(Stream(L"x"), L"show.s01e01.2160p.hevc.MKV") }) });
        auto const* record = result.Find(L"a:0");
        Require(record != nullptr, "the source was not resolved");

        auto downloading = Download();
        downloading.Status = DownloadStatus::Downloading;
        downloading.Fraction = 0.42;
        Require(SameState(Sources::SaveStateFor(record, &downloading, false), SaveKind::Saving, false, 0.42), "a running download did not read as saving with its fraction");
        auto queued = Download();
        queued.Status = DownloadStatus::Queued;
        queued.Fraction.reset();
        Require(SameState(Sources::SaveStateFor(record, &queued, false), SaveKind::Saving), "a queued download did not read as saving");
        // Starting it again leads to Downloads, where a stopped transfer resumes.
        auto paused = Download();
        paused.Status = DownloadStatus::Paused;
        Require(SameState(Sources::SaveStateFor(record, &paused, false), SaveKind::Ready), "a paused download did not read as ready");
        auto failed = Download();
        failed.Status = DownloadStatus::Failed;
        Require(SameState(Sources::SaveStateFor(record, &failed, false), SaveKind::Ready), "a failed download did not read as ready");
        Require(SameState(Sources::SaveStateFor(record, nullptr, true), SaveKind::Saving), "a start in flight did not read as saving");
        Require(SameState(Sources::SaveStateFor(nullptr, nullptr, false), SaveKind::Unavailable), "a missing source could be saved");

        // A nameless stream parses to the placeholder, which never matches a saved file.
        StreamRecord bare;
        bare.Url = L"https://cdn.test/1";
        auto const nameless = Resolve({ Group(L"a", L"Addon", { bare }) });
        auto const* namelessRecord = nameless.Find(L"a:0");
        Require(namelessRecord != nullptr, "the nameless source was not resolved");
        auto placeholder = Download();
        placeholder.ReleaseName = namelessRecord->Info.Filename.c_str();
        Require(SameState(Sources::SaveStateFor(namelessRecord, &placeholder, false), SaveKind::Ready, true),
            "the shared placeholder name matched a saved file");

        // A source with no address is dropped from the resolve before anything can save it.
        StreamRecord noUrl = Stream(L"Show.S01E01.1080p");
        noUrl.Url = L"";
        Require(Resolve({ Group(L"a", L"Addon", { noUrl }) }).Pool.empty(), "a stream without an address was listed");
    }

    void TestSaveLabels()
    {
        Require(Sources::SaveLabel({ SaveKind::Ready, false, std::nullopt }) == L"Save for offline", "the ready label changed");
        Require(Sources::SaveLabel({ SaveKind::Ready, true, std::nullopt }) == L"Save instead", "the replacing label changed");
        Require(Sources::SaveLabel({ SaveKind::Saving, false, std::nullopt }) == L"Saving", "saving with no size did not read Saving");
        Require(Sources::SaveLabel({ SaveKind::Saving, false, 0.429 }) == L"Saving 42%", "the saving percentage was not floored");
        Require(Sources::SaveLabel({ SaveKind::Saved, false, std::nullopt }) == L"Saved", "the saved label changed");
        Require(Sources::SaveLabel({ SaveKind::Unavailable, false, std::nullopt }).empty(), "an unavailable source had a label");
    }

    void TestOfflineCopy()
    {
        auto film = Download();
        film.ReleaseName = L"Film.2024.2160p.DV.HEVC.Atmos.mkv";
        film.SubtitleLanguage = L"eng";
        auto const copy = Sources::OfflineCopyFor(film);
        Require(copy.JobId == L"job1", "the saved copy lost its job");
        Require(copy.Medal == Medal{ L"4K", L"DV", MedalFinish::Gold }, "the saved copy's medal was not read from its release");
        Require(copy.Meta == L"HEVC \x00B7 13.0 GB \x00B7 ENG", "the saved copy's meta line was wrong");

        auto bareDownload = Download();
        bareDownload.ReleaseName = L"Home video.mkv";
        bareDownload.SizeBytes = 0;
        auto const bare = Sources::OfflineCopyFor(bareDownload);
        Require(!bare.Medal, "a release with no resolution got a medal");
        Require(bare.Meta == L"NO SUBS", "a bare saved copy did not say it has no subtitles");

        // The stream's own name can say what the file name does not.
        auto named = Download();
        named.ReleaseName = L"video.mkv";
        named.StreamName = L"Addon 1080p";
        Require(Sources::OfflineCopyFor(named).Medal == Medal{ L"1080p", L"FHD", MedalFinish::Silver }, "the stream name was not read for the saved copy");
    }

    void TestVideoDownloadChoice()
    {
        auto running = Download();
        running.JobId = L"new";
        running.Status = DownloadStatus::Downloading;
        auto done = Download();
        auto const both = Sources::ChooseVideoDownloads({ running, done });
        Require(both.Ready && both.Ready->JobId == L"job1", "the finished download was not the saved copy");
        Require(both.Current && both.Current->JobId == L"job1", "the finished download did not decide save states");
        auto const onlyRunning = Sources::ChooseVideoDownloads({ running });
        Require(!onlyRunning.Ready, "a running download was offered as a saved copy");
        Require(onlyRunning.Current && onlyRunning.Current->JobId == L"new", "a running download did not decide save states");
        Require(!Sources::ChooseVideoDownloads({}).Current, "no download decided save states");
    }

    void TestReleaseNames()
    {
        using HaloDesktop::Services::SameReleaseFile;
        Require(SameReleaseFile(L"C:\\dl\\Movie.2024.mkv", L"movie.2024.MKV"), "a saved path did not match its leaf case-insensitively");
        Require(!SameReleaseFile(L"", L""), "two empty names matched");
        Require(!SameReleaseFile(L"Movie.mkv", L"Movie.mp4"), "different extensions matched");
    }

    void TestSummaryAndFailures()
    {
        Require(Sources::BuildResolveSummary(240, 3, 0) == L"240 sources \x00B7 3 addons", "the plain count line changed");
        Require(Sources::BuildResolveSummary(1, 1, 0) == L"1 source \x00B7 1 addon", "the singular count line changed");
        Require(Sources::BuildResolveSummary(5, 3, 1) == L"5 sources \x00B7 3 of 4 addons", "the ratio form did not appear on a failure");
        Require(Sources::BuildResolveSummary(0, 0, 1) == L"0 sources \x00B7 0 of 1 addon", "the all-failed count line changed");
        Require(Sources::BuildResolveSummary(0, 0, 0) == L"No addon answered", "no addons did not say so");

        auto const result = Resolve({}, { AddonFailure{ winrt::hstring{ L"Torrentio" }, winrt::hstring{ L"timeout" } } });
        Require(result.FirstFailure == L"Torrentio did not answer in time", "the banner title for one failure was wrong");
        Require(result.Providers.size() == 1 && result.Providers[0].Name == L"Torrentio"
                && result.Providers[0].Value == L"did not answer in time" && !result.Providers[0].Answered,
            "a failed provider did not carry its failure copy into the footer");
        Require(HaloDesktop::Services::AddonFailureCopy(winrt::hstring{ L"upstream_http" }) == L"returned an error", "the upstream failure copy changed");
        Require(HaloDesktop::Services::AddonFailureCopy(std::nullopt) == L"is unavailable", "an unknown failure did not read unavailable");
    }

    void TestListing()
    {
        auto const result = Resolve({
            Group(L"a", L"Alpha", { Stream(L"A.2160p [RD+] 20 GB"), Stream(L"A.720p 1 GB") }),
            Group(L"b", L"Beta", { Stream(L"B.1080p [RD+] 6 GB"), Stream(L"B.1080p [RD download] 4 GB") }),
        });
        auto const listing = Sources::BuildListing(result, Options());
        Require(listing.Best && listing.Best->Entry.Key == L"a:0", "the pick was not the top source");
        Require(listing.Best->Status == L"Plays instantly", "the pick's status line was wrong");
        Require(listing.Items.size() == 3
                && listing.Items[0].Type == Sources::ListItem::Kind::Header && listing.Items[0].Name == L"Beta" && listing.Items[0].Count == 1
                && listing.Items[1].Type == Sources::ListItem::Kind::Row && listing.Items[1].Entry.Key == L"b:0"
                && listing.Items[2].Type == Sources::ListItem::Kind::More
                && listing.Items[2].Label == L"Show 2 sources that need downloading first",
            "the listing did not group by provider, lift the pick and hide the cold sources");
        Require(Stops(listing) == std::vector<std::wstring>{ L"a:0", L"b:0" }, "the keyboard stops were wrong");

        auto const revealed = Sources::BuildListing(result, Options(Sources::SourceFilter::All, Sources::SourceSort::Recommended, true));
        std::vector<std::wstring> headers;
        for (auto const& item : revealed.Items)
        {
            if (item.Type == Sources::ListItem::Kind::Header) headers.push_back(std::wstring{ item.Name.c_str() } + L" " + std::to_wstring(item.Count));
        }
        Require(headers == std::vector<std::wstring>{ L"Beta 2", L"Alpha 1" }, "revealing the cold sources grouped them wrongly");

        // Any other sort drops the pick block and shows everything.
        auto const bySize = Sources::BuildListing(result, Options(Sources::SourceFilter::All, Sources::SourceSort::SmallestFile));
        Require(!bySize.Best, "a sort other than Recommended kept the pick");
        std::size_t rows{};
        for (auto const& item : bySize.Items) rows += item.Type == Sources::ListItem::Kind::Row ? 1 : 0;
        Require(rows == 4, "a sort other than Recommended hid sources");
        Require(Sources::CountFilters(result.Pool) == Sources::FilterCounts{ 4, 1, 2, 1 }, "the filter counts were wrong");

        // A filter that leaves the top source out has no pick; the rest still group.
        auto const fullHd = Sources::BuildListing(result, Options(Sources::SourceFilter::FullHd, Sources::SourceSort::Recommended, true));
        Require(!fullHd.Best, "a filter that excluded the top source still had a pick");
        Require(Stops(fullHd) == std::vector<std::wstring>{ L"b:0", L"b:1" }, "the filtered stops were wrong");

        auto const mix = Sources::QualityMix(result.Pool);
        Require(mix.size() == 3 && mix[0].Count == 1 && mix[1].Count == 2 && mix[2].Count == 1 && std::abs(mix[1].Share - 0.5) < 1e-9,
            "the quality mix was wrong");
        Require(std::wstring_view{ Sources::SortName(Sources::SourceSort::FastestStart) } == L"Fastest start", "a sort name changed");
    }

    void TestMetaLine()
    {
        auto withSubtitles = Stream(L"A.2160p DV HEVC DDP 5.1 ENG JPN 8.42 GB");
        withSubtitles.Subtitles.push_back({ winrt::hstring{ L"1" }, winrt::hstring{ L"u" }, winrt::hstring{ L"eng" } });
        auto const pool = Resolve({ Group(L"a", L"Alpha", { withSubtitles, Stream(L"A.1080p") }) }).Pool;
        Require(pool.size() == 2, "both sources were not listed");
        Require(Sources::MetaLine(pool[0]) == L"HEVC \x00B7 DDP 5.1 \x00B7 8.42 GB", "the meta line was not codec, sound and size");
        Require(Sources::MetaLine(pool[1]).empty(), "a release naming nothing had a meta line");

        auto const single = Resolve({ Group(L"a", L"Alpha", { Stream(L"A.2160p HEVC Atmos [RD+] 20 GB") }) });
        auto const listing = Sources::BuildListing(single, Options());
        Require(listing.Best && listing.Best->Meta == L"HEVC \x00B7 ATMOS \x00B7 20.0 GB", "the pick's meta line was wrong");
    }

    void TestBitrateWarning()
    {
        auto const result = Resolve(
            { Group(L"a", L"Alpha", { Stream(L"A.2160p [RD+] 20 GB"), Stream(L"A.1080p [RD+] 2 GB") }) },
            {},
            3600.0);
        auto const listing = Sources::BuildListing(result, Options(
            Sources::SourceFilter::All,
            Sources::SourceSort::Recommended,
            false,
            Sources::DeviceContext{ .LineMbps = 10.0, .DurationSeconds = 3600.0 }));
        Require(listing.Best && listing.Best->Warning == L"Needs 48 Mbps, more than your line", "a source too heavy for the line did not say so");
        auto const row = std::find_if(listing.Items.begin(), listing.Items.end(), [](Sources::ListItem const& item)
        {
            return item.Type == Sources::ListItem::Kind::Row;
        });
        Require(row != listing.Items.end() && row->Warning.empty(), "a source the line can carry carried a warning");
    }

    void TestDetails()
    {
        auto const measured = Resolve(
            { Group(L"a", L"Alpha", { Stream(L"A.1080p HEVC 10bit DDP 5.1 [RD+] 3 GB") }) },
            {},
            3600.0).Pool;
        Require(measured.size() == 1, "the measured source was not listed");
        auto const details = Sources::DetailsFor(measured[0], Sources::DeviceContext{ .LineMbps = 70.0, .DurationSeconds = 3600.0 });
        // No HDR, no audio languages: left out, never spelled out as missing.
        Require(details.Rows == std::vector<Sources::DetailRow>{
                    { L"Picture", L"1920 \x00D7 1080 \x00B7 H.265 10-bit" },
                    { L"Sound", L"Surround 5.1" },
                    { L"Subtitles", L"None" },
                },
            "the details named facts the release did not, or missed one it did");
        Require(details.CacheLabel == L"Cached" && details.CacheGood, "a cached source did not say so");
        Require(details.Bandwidth && details.Bandwidth->Label == L"7 Mbps", "the bandwidth was not measured");
        Require(details.Bandwidth->Note == L"10% of your 70 Mbps line", "the bandwidth note was wrong");
        Require(std::abs(details.Bandwidth->Fraction - 0.1) < 0.01, "the meter was not measured against the line");
        Require(details.Provider == L"Alpha", "the details did not name the provider");

        auto const unmeasured = Resolve({ Group(L"a", L"Alpha", { Stream(L"A.2160p DV HEVC") }) }, {}, 3600.0).Pool;
        auto const bare = Sources::DetailsFor(unmeasured[0], Sources::DeviceContext{});
        Require(!bare.Rows.empty() && bare.Rows[0].Value == L"3840 \x00D7 2160 \x00B7 Dolby Vision \x00B7 H.265", "the picture fact was wrong");
        Require(!bare.Bandwidth, "a source with no size was measured");
        Require(bare.CacheLabel.empty(), "an unreported cache was described");

        auto languages = Stream(L"A.720p AAC ENG GER");
        languages.Subtitles.push_back({ winrt::hstring{ L"1" }, winrt::hstring{ L"u" }, winrt::hstring{ L"spa" } });
        auto const spoken = Sources::DetailsFor(Resolve({ Group(L"a", L"Alpha", { languages }) }).Pool[0], Sources::DeviceContext{});
        Require(spoken.Rows.size() == 3 && spoken.Rows[1].Value == L"Stereo \x00B7 English, German" && spoken.Rows[2].Value == L"Spanish",
            "the sound and subtitle facts did not name the languages");
    }

    void TestMedals()
    {
        auto const medal = [](wchar_t const* quality, wchar_t const* range)
        {
            return HaloDesktop::ViewModels::MedalFor(quality, range);
        };
        Require(medal(L"2160p", L"DV") == Medal{ L"4K", L"DV", MedalFinish::Gold }, "a Dolby Vision 4K medal was wrong");
        Require(medal(L"2160p", L"SDR") == Medal{ L"4K", L"UHD", MedalFinish::Gold }, "a 4K medal was wrong");
        Require(medal(L"1080p", L"HDR10") == Medal{ L"1080p", L"HDR", MedalFinish::Silver }, "an HDR medal was wrong");
        Require(medal(L"1440p", L"SDR") == Medal{ L"1440p", L"QHD", MedalFinish::Silver }, "a 1440p medal was wrong");
        Require(medal(L"720p", L"SDR") == Medal{ L"720p", L"HD", MedalFinish::Bronze }, "an HD medal was wrong");
        Require(medal(L"480p", L"SDR") == Medal{ L"480p", L"", MedalFinish::Plain }, "a lower medal had a tag");
        // The reference plates an unparsed quality with an em dash; this repository
        // uses none, so the plate carries an en dash.
        Require(medal(L"UNKNOWN", L"SDR") == Medal{ L"\x2013", L"", MedalFinish::Plain }, "an unparsed medal was wrong");
    }

    void TestGluedChannels()
    {
        Require(Audio(L"Show.S01E01.1080p.WEB-DL.DDP5.1.H.264") == L"DDP 5.1", "a glued DDP channel count was not read");
        Require(Audio(L"Film.2160p.DD+5.1.HEVC") == L"DDP 5.1", "DD+ was not read as DDP");
        Require(Audio(L"Film.1080p.DD+.x264") == L"DDP", "a bare DD+ was not read as DDP");
        Require(Audio(L"Film.720p.DD5.1.x264") == L"DD 5.1", "a glued DD channel count was not read");
        Require(Audio(L"Film.720p.WEB.AAC2.0.H.264") == L"AAC 2.0", "a glued AAC channel count was not read");
        Require(Audio(L"Film.2160p.DTS-HD.MA7.1") == L"DTS-HD", "DTS-HD was not read");
        Require(Audio(L"Film.2160p.TrueHD7.1.HEVC") == L"TRUEHD", "a glued TrueHD was not read");
        // A letter after the name is another word, not this codec.
        Require(!Audio(L"Film.1080p.Ddplus.x264"), "a word starting with a codec name was read as the codec");
    }

    void TestQualityLookalikes()
    {
        Require(!Quality(L"Movie.BluRay.DTS-HD.MA.5.1-GRP"), "DTS-HD claimed 720p");
        Require(!Quality(L"Movie.BluRay.DTSHD-GRP"), "DTSHD claimed 720p");
        Require(!Quality(L"Movie.Remux-GRP.ts\n\xD83D\xDCBE 20 GB"), "a .ts extension ending the first line claimed SD");
        // The real tokens still count.
        Require(Quality(L"Movie.HD.WEB.DTS-HD-GRP") == L"720p", "a real HD token was stripped with DTS-HD");
        Require(Quality(L"Movie.2023.TS.x264-GRP") == L"SD", "a real telesync token was not read");
        Require(Quality(L"Movie.1080p.DTS-HD.MA.5.1") == L"1080p", "a real resolution was lost beside DTS-HD");

        // `.ts` counts at the end of ANY line, whatever MSVC's `$` would match: a
        // line ending in CRLF, a middle line, the last line, and the file name
        // hint (which is searched last), but not mid-line.
        Require(!Quality(L"Movie.Remux.ts\r\nseeders 12"), "a .ts before a CRLF claimed SD");
        Require(!Quality(L"first\nMovie.Remux.TS\nlast"), "a .TS ending a middle line claimed SD");
        Require(!Quality(L"first\nMovie.Remux.ts"), "a .ts ending the last line claimed SD");
        Require(!HaloDesktop::Services::ParseStreamInfo(WithFilename(Stream(L"Movie Remux"), L"Movie.Remux.ts")).Quality,
            "a .ts file name hint claimed SD");
        Require(Quality(L"Movie.ts.Remux\nnext") == L"SD", "a .ts in the middle of a line stopped reading as a telesync token");
    }

    void TestWholeBytes()
    {
        // 29.35 GB multiplies out to 31514322534.4 bytes.
        Require(Size(L"Inception.2010.2160p.mkv 29.35 GB") == 31514322534ull, "a fractional size was not rounded to whole bytes");
        Require(Size(L"Movie.mkv 700.5 MB") == 734527488ull, "a megabyte size was wrong");
        // An addon's own fractional figure is rounded too, and one of zero means unknown.
        Require(HaloDesktop::Services::WholeBytes(1234.6) == 1235ull, "a fractional byte count was not rounded");
        Require(Size(L"Movie.mkv 2 GB", 0) == 2 * GiB, "a zero size hint hid the size in the text");
        Require(!Size(L"Movie.mkv"), "a release naming no size got one");
        Require(Size(L"Movie.mkv 2 GB", 5 * MiB) == 5 * MiB, "the size hint did not win over the text");
        Require(!HaloDesktop::Services::WholeBytes(0.0), "zero bytes was a size");
        Require(!HaloDesktop::Services::WholeBytes(-3.0), "a negative size was a size");
        Require(!HaloDesktop::Services::WholeBytes(std::numeric_limits<double>::quiet_NaN()), "NaN was a size");
        Require(!HaloDesktop::Services::WholeBytes(std::numeric_limits<double>::infinity()), "infinity was a size");
        Require(!HaloDesktop::Services::WholeBytes(0.4), "a size that rounds to nothing was a size");
    }

    void TestKicker()
    {
        Require(Sources::KickerFor(L"Pilot", L"Show", L"S01E01") == L"S01E01 \x00B7 Show", "an episode's kicker did not name the show");
        Require(Sources::KickerFor(L"Show", L"Show", L"S01E01") == L"S01E01", "a kicker repeated the heading");
        Require(Sources::KickerFor(L"Film", L"Film", L"").empty(), "a movie had a kicker");
        Require(Sources::KickerFor(L"Pilot", L"Show", L"") == L"Show", "a resumed entry did not name the show");
    }
}

void RunSourcesSheetTests()
{
    TestSizesAndStatus();
    TestAddonNames();
    TestSavedDownloadStaysOutOfTheList();
    TestSaveStates();
    TestSaveLabels();
    TestOfflineCopy();
    TestVideoDownloadChoice();
    TestReleaseNames();
    TestSummaryAndFailures();
    TestListing();
    TestMetaLine();
    TestBitrateWarning();
    TestDetails();
    TestMedals();
    TestGluedChannels();
    TestQualityLookalikes();
    TestWholeBytes();
    TestKicker();
}
