#include "pch.h"
#include "ViewModels/SourcesModel.h"

#include "Services/AddonNamePolicy.h"
#include "Services/DownloadSourceMatch.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string_view>
#include <utility>

namespace
{
    namespace Sources = ::HaloDesktop::Sources;
    namespace Downloads = ::HaloDesktop::Services::Downloads;

    std::wstring_view View(winrt::hstring const& value) noexcept
    {
        return std::wstring_view{ value.c_str(), value.size() };
    }

    std::wstring Upper(std::wstring_view value)
    {
        std::wstring result{ value };
        std::transform(result.begin(), result.end(), result.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towupper(character));
        });
        return result;
    }

    Sources::SourceStatus StatusOf(::HaloDesktop::Services::ParsedStreamInfo const& info) noexcept
    {
        if (!info.Cached) return Sources::SourceStatus::Unknown;
        return *info.Cached ? Sources::SourceStatus::Instant : Sources::SourceStatus::Uncached;
    }

    winrt::hstring JoinLanguages(std::vector<winrt::hstring> const& languages)
    {
        if (languages.empty()) return winrt::hstring{ Sources::NotParsed };
        std::wstring result;
        for (auto const& language : languages)
        {
            if (!result.empty()) result.append(L" \x00B7 ");
            result.append(language.c_str());
        }
        return winrt::hstring{ result };
    }

    Sources::SourceEntry MakeEntry(
        Sources::SourceRecord const& record,
        winrt::hstring const& providerId,
        winrt::hstring const& provider,
        std::int32_t rank,
        double durationSeconds)
    {
        auto const& info = record.Info;
        Sources::SourceEntry entry;
        entry.Key = record.Key;
        entry.ProviderId = providerId;
        entry.Provider = provider;
        entry.Status = StatusOf(info);
        entry.Quality = info.Quality.value_or(winrt::hstring{ Sources::NotParsed });
        entry.Range = info.DynamicRange.value_or(L"SDR");
        entry.Codec = info.Codec.value_or(winrt::hstring{ Sources::NotParsed });
        entry.Audio = info.Audio.value_or(winrt::hstring{ Sources::NotParsed });
        entry.Languages = JoinLanguages(info.Languages);
        entry.Size = ::HaloDesktop::Services::FormatStreamSize(info.SizeBytes);
        entry.File = info.Filename;
        for (auto const& subtitle : record.Stream.Subtitles)
        {
            if (!subtitle.Lang.empty()) entry.SubtitleLanguages.push_back(subtitle.Lang);
        }
        entry.Tier = Sources::TierOf(View(entry.Quality));
        entry.Speed = Sources::SpeedOf(entry.Status);
        entry.SizeBytes = info.SizeBytes.value_or(0);
        entry.Rank = rank;
        if (entry.SizeBytes > 0 && durationSeconds > 0.0)
        {
            entry.NeededMbps = static_cast<double>(entry.SizeBytes) * 8.0 / durationSeconds / 1'000'000.0;
        }
        return entry;
    }

    std::wstring ToKey(winrt::hstring const& key)
    {
        return std::wstring{ key.c_str(), key.size() };
    }

    template <typename Row>
    Row Describe(Sources::SourceEntry const& entry, Sources::DeviceContext const& device)
    {
        Row row;
        row.Entry = entry;
        row.Status = Sources::StatusLabel(entry.Status);
        row.Meta = Sources::MetaLine(entry);
        row.Warning = Sources::BitrateWarning(entry, device.LineMbps);
        row.Details = Sources::DetailsFor(entry, device);
        return row;
    }
}

namespace HaloDesktop::Sources
{
    wchar_t const* SortName(SourceSort sort) noexcept
    {
        switch (sort)
        {
        case SourceSort::BestPicture: return L"Best picture";
        case SourceSort::SmallestFile: return L"Smallest file";
        case SourceSort::FastestStart: return L"Fastest start";
        case SourceSort::Recommended: break;
        }
        return L"Recommended";
    }

    ParsedResolve ParseResolve(
        Api::Dto::StreamsPayload const& payload,
        std::function<winrt::hstring(std::size_t group, std::size_t index)> const& key)
    {
        ParsedResolve result;
        result.Groups.reserve(payload.Results.size());
        for (std::size_t groupIndex{}; groupIndex < payload.Results.size(); ++groupIndex)
        {
            auto const& group = payload.Results[groupIndex];
            AddonStreamGroup parsed{ group.AddonId, group.AddonName, {} };
            parsed.Streams.reserve(group.Streams.size());
            for (std::size_t index{}; index < group.Streams.size(); ++index)
            {
                auto const& stream = group.Streams[index];
                parsed.Streams.push_back({ key(groupIndex, index), stream, Services::ParseStreamInfo(stream) });
            }
            result.Groups.push_back(std::move(parsed));
        }
        result.Errors = payload.Errors;
        return result;
    }

    SourceRecord const* Resolve::Find(winrt::hstring const& key) const
    {
        auto const found = Records.find(ToKey(key));
        return found == Records.end() ? nullptr : &found->second;
    }

    winrt::hstring BuildResolveSummary(
        std::size_t sourceCount,
        std::size_t answeredProviders,
        std::size_t failedProviders)
    {
        auto const addons = answeredProviders + failedProviders;
        if (addons == 0) return L"No addon answered";
        auto const asked = failedProviders == 0
            ? CountLabel(addons, L"addon", L"addons")
            : winrt::to_hstring(answeredProviders) + L" of " + CountLabel(addons, L"addon", L"addons");
        return CountLabel(sourceCount, L"source", L"sources") + L" \x00B7 " + asked;
    }

    Resolve ResolveSources(ParsedResolve const& parsed, double durationSeconds)
    {
        Resolve result;
        std::vector<std::vector<SourceRecord const*>> groupRecords;
        groupRecords.reserve(parsed.Groups.size());
        std::vector<SourceRecord const*> ranked;
        for (auto const& group : parsed.Groups)
        {
            std::vector<SourceRecord const*> kept;
            for (auto const& stream : group.Streams)
            {
                if (stream.Stream.Url.empty()) continue;
                auto const [position, inserted] = result.Records.emplace(
                    ToKey(stream.Key),
                    SourceRecord{ stream.Key, group.AddonId, stream.Stream, stream.Info });
                if (!inserted) continue;
                kept.push_back(&position->second);
                ranked.push_back(&position->second);
            }
            groupRecords.push_back(std::move(kept));
        }

        // Pointers into an unordered_map stay valid across later insertions, so the
        // ranking and the groups can share them.
        std::stable_sort(ranked.begin(), ranked.end(), [](SourceRecord const* left, SourceRecord const* right)
        {
            return Services::CompareStreams(left->Info, right->Info) < 0;
        });
        std::unordered_map<std::wstring, std::int32_t> rankOf;
        for (std::size_t position{}; position < ranked.size(); ++position)
        {
            rankOf.emplace(ToKey(ranked[position]->Key), static_cast<std::int32_t>(position));
        }

        for (std::size_t index{}; index < parsed.Groups.size(); ++index)
        {
            auto const& group = parsed.Groups[index];
            auto const name = Services::SanitizeAddonName(group.AddonName);
            auto const providerId = group.AddonId.empty() ? name : group.AddonId;
            for (auto const* record : groupRecords[index])
            {
                result.Pool.push_back(MakeEntry(*record, providerId, name, rankOf.at(ToKey(record->Key)), durationSeconds));
            }
            result.Providers.push_back({ name, CountLabel(groupRecords[index].size(), L"source", L"sources"), true });
        }
        for (auto const& failure : parsed.Errors)
        {
            auto const name = Services::SanitizeAddonName(failure.Name);
            auto const note = Services::AddonFailureCopy(failure.Code);
            if (result.FirstFailure.empty()) result.FirstFailure = name + L" " + note;
            result.Providers.push_back({ name, note, false });
        }
        std::stable_sort(result.Pool.begin(), result.Pool.end(), [](SourceEntry const& left, SourceEntry const& right)
        {
            return left.Rank < right.Rank;
        });

        result.AnsweredProviders = parsed.Groups.size();
        result.FailedProviders = parsed.Errors.size();
        result.Summary = BuildResolveSummary(result.Pool.size(), result.AnsweredProviders, result.FailedProviders);
        return result;
    }

    bool MatchesFilter(SourceEntry const& entry, SourceFilter filter) noexcept
    {
        switch (filter)
        {
        case SourceFilter::UltraHd: return entry.Tier == QualityTier::UltraHd;
        case SourceFilter::FullHd: return entry.Tier == QualityTier::FullHd;
        case SourceFilter::Hd: return entry.Tier == QualityTier::Hd;
        case SourceFilter::All: break;
        }
        return true;
    }

    std::vector<SourceEntry> SortEntries(std::vector<SourceEntry> entries, SourceSort sort)
    {
        switch (sort)
        {
        case SourceSort::BestPicture:
            std::stable_sort(entries.begin(), entries.end(), [](SourceEntry const& left, SourceEntry const& right)
            {
                if (left.Tier != right.Tier) return left.Tier < right.Tier;
                return left.SizeBytes > right.SizeBytes;
            });
            break;
        case SourceSort::SmallestFile:
            // A source that never reported a size cannot claim to be the smallest.
            std::stable_sort(entries.begin(), entries.end(), [](SourceEntry const& left, SourceEntry const& right)
            {
                if ((left.SizeBytes == 0) != (right.SizeBytes == 0)) return right.SizeBytes == 0;
                return left.SizeBytes < right.SizeBytes;
            });
            break;
        case SourceSort::FastestStart:
            std::stable_sort(entries.begin(), entries.end(), [](SourceEntry const& left, SourceEntry const& right)
            {
                if (left.Speed != right.Speed) return left.Speed < right.Speed;
                return left.Rank < right.Rank;
            });
            break;
        case SourceSort::Recommended:
            std::stable_sort(entries.begin(), entries.end(), [](SourceEntry const& left, SourceEntry const& right)
            {
                return left.Rank < right.Rank;
            });
            break;
        }
        return entries;
    }

    Listing BuildListing(Resolve const& resolve, ListingOptions const& options)
    {
        std::vector<SourceEntry> matching;
        for (auto const& entry : resolve.Pool)
        {
            if (MatchesFilter(entry, options.Filter)) matching.push_back(entry);
        }
        auto const listed = SortEntries(std::move(matching), options.Sort);
        SourceEntry const* pick = options.Sort == SourceSort::Recommended
                && !resolve.Pool.empty()
                && MatchesFilter(resolve.Pool.front(), options.Filter)
            ? &resolve.Pool.front()
            : nullptr;

        struct Bucket final
        {
            winrt::hstring Id;
            winrt::hstring Name;
            std::vector<SourceEntry const*> Entries;
        };
        std::vector<Bucket> buckets;
        std::size_t hiddenCold{};
        for (auto const& entry : listed)
        {
            if (pick && entry.Key == pick->Key) continue;
            if (options.Sort == SourceSort::Recommended && !options.ColdRevealed && entry.Speed == StartSpeed::NeedsDownload)
            {
                ++hiddenCold;
                continue;
            }
            auto const bucket = std::find_if(buckets.begin(), buckets.end(), [&entry](Bucket const& candidate)
            {
                return candidate.Id == entry.ProviderId;
            });
            if (bucket != buckets.end()) bucket->Entries.push_back(&entry);
            else buckets.push_back({ entry.ProviderId, entry.Provider, { &entry } });
        }

        Listing result;
        for (auto const& bucket : buckets)
        {
            ListItem header;
            header.Type = ListItem::Kind::Header;
            header.Name = bucket.Name;
            header.Count = bucket.Entries.size();
            result.Items.push_back(std::move(header));
            for (auto const* entry : bucket.Entries)
            {
                auto row = Describe<ListItem>(*entry, options.Device);
                row.Type = ListItem::Kind::Row;
                result.Items.push_back(std::move(row));
            }
        }
        if (hiddenCold != 0)
        {
            ListItem more;
            more.Type = ListItem::Kind::More;
            more.Label = L"Show " + CountLabel(hiddenCold, L"source", L"sources") + L" that need downloading first";
            result.Items.push_back(std::move(more));
        }
        if (pick) result.Best = Describe<Pick>(*pick, options.Device);
        return result;
    }

    std::size_t FilterCounts::For(SourceFilter filter) const noexcept
    {
        switch (filter)
        {
        case SourceFilter::UltraHd: return UltraHd;
        case SourceFilter::FullHd: return FullHd;
        case SourceFilter::Hd: return Hd;
        case SourceFilter::All: break;
        }
        return All;
    }

    FilterCounts CountFilters(std::vector<SourceEntry> const& pool) noexcept
    {
        FilterCounts counts;
        for (auto const& entry : pool)
        {
            if (MatchesFilter(entry, SourceFilter::All)) ++counts.All;
            if (MatchesFilter(entry, SourceFilter::UltraHd)) ++counts.UltraHd;
            if (MatchesFilter(entry, SourceFilter::FullHd)) ++counts.FullHd;
            if (MatchesFilter(entry, SourceFilter::Hd)) ++counts.Hd;
        }
        return counts;
    }

    std::vector<QualityShare> QualityMix(std::vector<SourceEntry> const& pool)
    {
        std::vector<QualityShare> result;
        for (auto const tier : { QualityTier::UltraHd, QualityTier::FullHd, QualityTier::Hd })
        {
            auto const count = static_cast<std::size_t>(std::count_if(pool.begin(), pool.end(), [tier](SourceEntry const& entry)
            {
                return entry.Tier == tier;
            }));
            auto const share = pool.empty() ? 0.0 : static_cast<double>(count) / static_cast<double>(pool.size());
            result.push_back({ tier, count, share });
        }
        return result;
    }

    std::vector<winrt::hstring> SelectionStops(Listing const& listing)
    {
        std::vector<winrt::hstring> stops;
        if (listing.Best) stops.push_back(listing.Best->Entry.Key);
        for (auto const& item : listing.Items)
        {
            if (item.Type == ListItem::Kind::Row) stops.push_back(item.Entry.Key);
        }
        return stops;
    }

    VideoDownloads ChooseVideoDownloads(std::vector<Downloads::VideoDownload> const& downloads)
    {
        VideoDownloads result;
        auto const ready = std::find_if(downloads.begin(), downloads.end(), [](Downloads::VideoDownload const& item)
        {
            return item.Status == Downloads::DownloadStatus::Done;
        });
        if (ready != downloads.end()) result.Ready = *ready;
        if (result.Ready) result.Current = result.Ready;
        else if (!downloads.empty()) result.Current = downloads.front();
        return result;
    }

    OfflineCopy OfflineCopyFor(Downloads::VideoDownload const& download)
    {
        Api::Dto::StreamRecord stream;
        if (download.StreamName && !download.StreamName->empty()) stream.Name = winrt::hstring{ *download.StreamName };
        if (download.StreamTitle && !download.StreamTitle->empty()) stream.Title = winrt::hstring{ *download.StreamTitle };
        stream.Filename = winrt::hstring{ download.ReleaseName };
        auto const info = Services::ParseStreamInfo(stream);

        std::wstring meta;
        auto const append = [&meta](std::wstring_view part)
        {
            if (part.empty()) return;
            if (!meta.empty()) meta.append(L" \x00B7 ");
            meta.append(part);
        };
        if (info.Codec) append(View(*info.Codec));
        if (download.SizeBytes > 0)
        {
            auto const size = Services::FormatStreamSize(download.SizeBytes);
            append(View(size));
        }
        append(download.SubtitleLanguage && !download.SubtitleLanguage->empty()
            ? Upper(*download.SubtitleLanguage)
            : std::wstring{ L"NO SUBS" });

        OfflineCopy result;
        result.JobId = download.JobId;
        if (info.Quality)
        {
            auto const range = info.DynamicRange.value_or(L"SDR");
            result.Medal = ViewModels::MedalFor(View(*info.Quality), View(range));
        }
        result.Meta = winrt::hstring{ meta };
        return result;
    }

    SaveState SaveStateFor(
        SourceRecord const* record,
        Downloads::VideoDownload const* download,
        bool starting)
    {
        if (!record || record->Stream.Url.empty()) return { SaveState::Kind::Unavailable, false, std::nullopt };
        if (starting) return { SaveState::Kind::Saving, false, std::nullopt };
        if (!download) return { SaveState::Kind::Ready, false, std::nullopt };
        auto const fromHere = Services::HasIdentifyingFilename(record->Info)
            && Services::SameReleaseFile(download->ReleaseName, View(record->Info.Filename));
        if (!fromHere) return { SaveState::Kind::Ready, true, std::nullopt };
        switch (download->Status)
        {
        case Downloads::DownloadStatus::Done:
            return { SaveState::Kind::Saved, false, std::nullopt };
        case Downloads::DownloadStatus::Queued:
        case Downloads::DownloadStatus::Downloading:
            return { SaveState::Kind::Saving, false, download->Fraction };
        case Downloads::DownloadStatus::Paused:
        case Downloads::DownloadStatus::Failed:
            break;
        }
        return { SaveState::Kind::Ready, false, std::nullopt };
    }

    winrt::hstring SaveLabel(SaveState const& state)
    {
        switch (state.State)
        {
        case SaveState::Kind::Ready:
            return state.Replaces ? L"Save instead" : L"Save for offline";
        case SaveState::Kind::Saving:
            if (!state.Fraction) return L"Saving";
            return L"Saving " + winrt::to_hstring(static_cast<std::int64_t>(std::floor(*state.Fraction * 100.0))) + L"%";
        case SaveState::Kind::Saved:
            return L"Saved";
        case SaveState::Kind::Unavailable:
            break;
        }
        return {};
    }

    winrt::hstring KickerFor(
        winrt::hstring const& title,
        winrt::hstring const& showName,
        winrt::hstring const& episodeLabel)
    {
        auto const redundant = showName.empty() || showName == title;
        if (episodeLabel.empty()) return redundant ? winrt::hstring{} : showName;
        return redundant ? episodeLabel : episodeLabel + L" \x00B7 " + showName;
    }
}
