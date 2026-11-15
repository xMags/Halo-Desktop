#pragma once

#include "Api/Dto.h"
#include "Services/Downloads/DownloadTypes.h"
#include "Services/StreamInfo.h"
#include "ViewModels/QualityMedalPolicy.h"
#include "ViewModels/SourcePresentation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <winrt/base.h>

namespace HaloDesktop::Sources
{
    // The sources sheet's model, the reference's `sourcesModel.ts`: which sources
    // exist, how they rank, how the list groups them, what the header and footer
    // say about the resolve, the saved copy's card and each source's save state.
    // A finished download is not a source: it gets its own card above the list.
    // Pure: compiles without XAML, and nothing here fetches or reads state.

    enum class SourceFilter
    {
        All,
        UltraHd,
        FullHd,
        Hd,
    };
    inline constexpr std::array<SourceFilter, 4> Filters{
        SourceFilter::All, SourceFilter::UltraHd, SourceFilter::FullHd, SourceFilter::Hd };

    enum class SourceSort
    {
        Recommended,
        BestPicture,
        SmallestFile,
        FastestStart,
    };
    inline constexpr std::array<SourceSort, 4> Sorts{
        SourceSort::Recommended, SourceSort::BestPicture, SourceSort::SmallestFile, SourceSort::FastestStart };
    [[nodiscard]] wchar_t const* SortName(SourceSort sort) noexcept;

    // One stream as it came back, already parsed. The key is minted by the caller
    // once per resolve so a rebuild never renumbers the rows under an open sheet.
    struct ParsedStream final
    {
        winrt::hstring Key;
        Api::Dto::StreamRecord Stream;
        Services::ParsedStreamInfo Info;
    };

    struct AddonStreamGroup final
    {
        winrt::hstring AddonId;
        std::optional<winrt::hstring> AddonName;
        std::vector<ParsedStream> Streams;
    };

    // One /streams answer with every release name parsed.
    struct ParsedResolve final
    {
        std::vector<AddonStreamGroup> Groups;
        std::vector<Api::Dto::AddonFailure> Errors;
    };

    // Parses every stream of a payload. Release-name parsing is regex work, so
    // callers run this off the UI thread. `key` names stream `index` of group
    // `group`.
    [[nodiscard]] ParsedResolve ParseResolve(
        Api::Dto::StreamsPayload const& payload,
        std::function<winrt::hstring(std::size_t group, std::size_t index)> const& key);

    // What playing or saving a source needs; the display entry carries none of it.
    struct SourceRecord final
    {
        winrt::hstring Key;
        winrt::hstring AddonId;
        Api::Dto::StreamRecord Stream;
        Services::ParsedStreamInfo Info;
    };

    struct ProviderRow final
    {
        winrt::hstring Name;
        winrt::hstring Value;
        bool Answered{};
    };

    struct Resolve final
    {
        // Every source, in rank order.
        std::vector<SourceEntry> Pool;
        std::unordered_map<std::wstring, SourceRecord> Records;
        std::vector<ProviderRow> Providers;
        std::size_t AnsweredProviders{};
        std::size_t FailedProviders{};
        // `Torrentio did not answer in time`: the banner title for a single failure.
        winrt::hstring FirstFailure;
        // `240 sources . 3 addons`, the header's count line.
        winrt::hstring Summary;

        [[nodiscard]] SourceRecord const* Find(winrt::hstring const& key) const;
    };

    // The ratio form ("1 of 2 addons") only appears when somebody failed.
    [[nodiscard]] winrt::hstring BuildResolveSummary(
        std::size_t sourceCount,
        std::size_t answeredProviders,
        std::size_t failedProviders);

    // Ranking runs across the whole resolve, not within each addon. Streams with
    // no address are dropped.
    [[nodiscard]] Resolve ResolveSources(ParsedResolve const& parsed, double durationSeconds);

    [[nodiscard]] bool MatchesFilter(SourceEntry const& entry, SourceFilter filter) noexcept;
    // Stable, so equal keys keep their rank order.
    [[nodiscard]] std::vector<SourceEntry> SortEntries(std::vector<SourceEntry> entries, SourceSort sort);

    struct ListItem final
    {
        enum class Kind
        {
            Header,
            Row,
            More,
        };

        Kind Type{ Kind::Row };
        // Header: the provider and how many of its rows follow. More: the label.
        winrt::hstring Name;
        std::size_t Count{};
        winrt::hstring Label;
        // Row.
        SourceEntry Entry;
        winrt::hstring Status;
        winrt::hstring Meta;
        // Said in the details, beside the bandwidth.
        winrt::hstring Warning;
        SourceDetails Details;
    };

    struct Pick final
    {
        SourceEntry Entry;
        winrt::hstring Status;
        winrt::hstring Meta;
        winrt::hstring Warning;
        SourceDetails Details;
    };

    struct Listing final
    {
        std::optional<Sources::Pick> Best;
        std::vector<ListItem> Items;
    };

    struct ListingOptions final
    {
        SourceFilter Filter{ SourceFilter::All };
        SourceSort Sort{ SourceSort::Recommended };
        bool ColdRevealed{};
        DeviceContext Device;
    };

    // The recommended pick is the top-ranked source, shown in its own block and
    // not repeated below. The list groups the rest by provider in rank order;
    // under Recommended, sources that must download first wait behind one row.
    [[nodiscard]] Listing BuildListing(Resolve const& resolve, ListingOptions const& options);

    struct FilterCounts final
    {
        std::size_t All{};
        std::size_t UltraHd{};
        std::size_t FullHd{};
        std::size_t Hd{};

        [[nodiscard]] std::size_t For(SourceFilter filter) const noexcept;
        bool operator==(FilterCounts const&) const = default;
    };
    // How many sources each filter pill would show.
    [[nodiscard]] FilterCounts CountFilters(std::vector<SourceEntry> const& pool) noexcept;

    struct QualityShare final
    {
        QualityTier Tier{ QualityTier::Lower };
        std::size_t Count{};
        double Share{};
    };
    // The footer's quality mix: always the three tiers, even when one is empty.
    [[nodiscard]] std::vector<QualityShare> QualityMix(std::vector<SourceEntry> const& pool);

    // Keyboard stops, top to bottom: the pick, then every source row.
    [[nodiscard]] std::vector<winrt::hstring> SelectionStops(Listing const& listing);

    // This video's finished download and the download that decides save states:
    // the finished one when there is one (the engine keeps one per video), else
    // whichever there is.
    struct VideoDownloads final
    {
        std::optional<Services::Downloads::VideoDownload> Ready;
        std::optional<Services::Downloads::VideoDownload> Current;
    };
    [[nodiscard]] VideoDownloads ChooseVideoDownloads(std::vector<Services::Downloads::VideoDownload> const& downloads);

    // The finished download's card, above the filters and outside the ranking.
    struct OfflineCopy final
    {
        std::wstring JobId;
        // Empty when the release name does not say its resolution.
        std::optional<ViewModels::Medal> Medal;
        // `HEVC . 13.0 GB . ENG`: codec, size, and the saved subtitle or `NO SUBS`.
        winrt::hstring Meta;
    };
    [[nodiscard]] OfflineCopy OfflineCopyFor(Services::Downloads::VideoDownload const& download);

    // What saving a source means right now; one download per video decides it.
    struct SaveState final
    {
        enum class Kind
        {
            // Nothing to save from: the source has no address.
            Unavailable,
            Ready,
            Saving,
            Saved,
        };

        Kind State{ Kind::Unavailable };
        // Ready: the video is already held from another source.
        bool Replaces{};
        // Saving: zero to one, or empty while the size is unknown.
        std::optional<double> Fraction;

        bool operator==(SaveState const&) const = default;
    };

    // Only the source the download came from wears its state, matched by release
    // name since debrid links change between resolves. Any other source can be
    // saved instead, which replaces it after asking. A paused or failed transfer
    // of this same source reads as ready: starting it again leads to Downloads,
    // where it resumes.
    [[nodiscard]] SaveState SaveStateFor(
        SourceRecord const* record,
        Services::Downloads::VideoDownload const* download,
        bool starting);

    // "Save for offline", "Save instead", "Saving" / "Saving N%", "Saved"; empty
    // when there is nothing to save.
    [[nodiscard]] winrt::hstring SaveLabel(SaveState const& state);

    // Only what the heading does not already say: a movie's show name is its own
    // title, and an entry resumed from Continue watching names the show.
    [[nodiscard]] winrt::hstring KickerFor(
        winrt::hstring const& title,
        winrt::hstring const& showName,
        winrt::hstring const& episodeLabel);
}
