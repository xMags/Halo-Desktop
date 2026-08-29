#include "pch.h"
#include "Services/SourceService.h"

#include "Api/ApiClient.h"
#include "Models/Models.h"
#include "Security/ProtectedHttpHeaders.h"
#include "Services/DownloadSourceMatch.h"
#include "Services/Downloads/DownloadPreparation.h"
#include "Services/SettingsSyncService.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <cwctype>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <winrt/Windows.Foundation.h>

namespace
{
    winrt::hstring JoinLanguages(std::vector<winrt::hstring> const& languages)
    {
        if (languages.empty()) return L"UNKNOWN";
        std::wstring result;
        for (auto const& language : languages)
        {
            if (!result.empty()) result.append(L" \x00B7 ");
            result.append(language.c_str());
        }
        return winrt::hstring{ result };
    }

    winrt::HaloDesktop::StreamStatus DisplayStatus(
        HaloDesktop::Services::ParsedStreamInfo const& info,
        bool onDisk) noexcept
    {
        if (onDisk) return winrt::HaloDesktop::StreamStatus::OnDisk;
        if (!info.Cached) return winrt::HaloDesktop::StreamStatus::Unknown;
        return *info.Cached ? winrt::HaloDesktop::StreamStatus::Instant : winrt::HaloDesktop::StreamStatus::Uncached;
    }

    // Only the language tags travel with the display source. Subtitle ids and
    // URLs stay in the resolved record, the same way stream URLs do.
    winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> SubtitleLanguages(
        HaloDesktop::Api::Dto::StreamRecord const& stream)
    {
        std::vector<winrt::hstring> languages;
        languages.reserve(stream.Subtitles.size());
        for (auto const& subtitle : stream.Subtitles)
        {
            if (subtitle.Lang.empty()) continue;
            languages.push_back(subtitle.Lang);
        }
        return winrt::single_threaded_vector(std::move(languages)).GetView();
    }

    winrt::HaloDesktop::StreamSource MakeDisplaySource(
        HaloDesktop::Services::ResolvedSourceRecord const& resolved,
        bool onDisk)
    {
        auto const& info = resolved.Info;
        return winrt::make<winrt::HaloDesktop::implementation::StreamSource>(
            resolved.Key,
            info.Quality.value_or(L"UNKNOWN"),
            info.DynamicRange.value_or(L"SDR"),
            info.Filename,
            info.Codec.value_or(L"UNKNOWN"),
            info.Audio.value_or(L"UNKNOWN"),
            JoinLanguages(info.Languages),
            DisplayStatus(info, onDisk),
            HaloDesktop::Services::FormatStreamSize(info.SizeBytes),
            info.Detail,
            info.SizeBytes.value_or(0),
            resolved.Rank,
            SubtitleLanguages(resolved.Stream));
    }

    std::wstring Trim(std::wstring value)
    {
        auto const first = std::find_if_not(value.begin(), value.end(), [](wchar_t item) { return std::iswspace(item) != 0; });
        if (first == value.end()) return {};
        auto const last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t item) { return std::iswspace(item) != 0; }).base();
        return std::wstring(first, last);
    }

    std::wstring NormalizedLanguage(winrt::hstring const& language)
    {
        auto value = std::wstring{ language.c_str() };
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        if (value == L"fre") return L"fra";
        if (value == L"ger") return L"deu";
        if (value == L"chi") return L"zho";
        if (value == L"cze") return L"ces";
        if (value == L"dut") return L"nld";
        if (value == L"gre") return L"ell";
        if (value == L"rum") return L"ron";
        if (value == L"slo") return L"slk";
        return value;
    }

    bool LanguageMatches(winrt::hstring const& left, winrt::hstring const& right)
    {
        return NormalizedLanguage(left) == NormalizedLanguage(right);
    }

    std::map<std::wstring, std::wstring, std::less<>> DownloadHeaders(
        std::vector<std::pair<winrt::hstring, winrt::hstring>> const& values)
    {
        std::map<std::wstring, std::wstring, std::less<>> result;
        for (auto const& [key, value] : values)
        {
            result.emplace(std::wstring{ key.c_str() }, std::wstring{ value.c_str() });
        }
        return result;
    }

    // The ratio form ("3 of 4 providers") only appears when somebody failed; naming
    // it unconditionally would imply a problem on every healthy resolve. Files on
    // the device are counted separately because they answered nothing: they are not
    // a provider, and folding them in would misreport how many were reached.
    winrt::hstring BuildResolveSummary(
        std::size_t sourceCount,
        std::size_t answeredProviders,
        std::size_t failedProviders,
        std::size_t localCount,
        double elapsedSeconds)
    {
        auto const providers = answeredProviders + failedProviders;
        std::wostringstream summary;
        if (providers != 0)
        {
            summary << sourceCount << (sourceCount == 1 ? L" source from " : L" sources from ");
            if (failedProviders != 0) summary << answeredProviders << L" of ";
            summary << providers << (providers == 1 ? L" provider, found in " : L" providers, found in ")
                    << std::fixed << std::setprecision(1) << elapsedSeconds << L" seconds";
            if (localCount != 0) summary << L", plus " << localCount << L" saved on this device";
            return winrt::hstring{ summary.str() };
        }
        if (localCount == 0) return L"No provider answered";
        summary << localCount << (localCount == 1 ? L" file saved on this device" : L" files saved on this device");
        return winrt::hstring{ summary.str() };
    }

    HaloDesktop::Security::ProtectedHttpHeaders HashHeaders(
        std::vector<std::pair<winrt::hstring, winrt::hstring>> const& values)
    {
        HaloDesktop::Security::ProtectedHttpHeaders result;
        result.reserve(values.size());
        for (auto const& [name, value] : values)
        {
            result.push_back({ std::wstring{ name.c_str() }, std::wstring{ value.c_str() } });
        }
        return result;
    }
}

namespace HaloDesktop::Services
{
    SourceService::SourceService(
        std::shared_ptr<Api::ApiClient> api,
        std::shared_ptr<IDownloadService> downloads,
        std::shared_ptr<SettingsSyncService> settings)
        : m_api(std::move(api)), m_downloads(std::move(downloads)), m_settings(std::move(settings)),
          m_groups(winrt::single_threaded_vector<winrt::HaloDesktop::SourceGroup>().GetView()),
          m_localSources(winrt::single_threaded_vector<winrt::HaloDesktop::StreamSource>().GetView())
    {
        if (!m_api || !m_downloads || !m_settings) throw std::invalid_argument("SourceService requires its dependencies.");
    }

    concurrency::task<void> SourceService::LoadAsync(winrt::HaloDesktop::SourcesNavParams const& parameters)
    {
        if (!parameters || parameters.Type().empty() || parameters.VideoId().empty())
        {
            // Cleared before throwing. The caller reports this as a failed resolve and
            // still reads the local sources, so leaving the previous title's files in
            // place would offer them under this one's name.
            ClearResolve();
            throw std::invalid_argument("Source navigation parameters are incomplete.");
        }

        auto const version = ++m_requestVersion;
        auto const uiContext = winrt::apartment_context{};
        auto const started = std::chrono::steady_clock::now();
        std::optional<Api::Dto::StreamsPayload> payload;
        std::exception_ptr failure;
        try
        {
            payload = co_await m_api->GetStreamsAsync(parameters.Type(), parameters.VideoId());
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        auto const elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        co_await uiContext;
        if (version != m_requestVersion) co_return;

        m_parameters = parameters;
        m_payload = payload ? std::move(*payload) : Api::Dto::StreamsPayload{};
        m_elapsedSeconds = elapsed;
        m_streamKeys.clear();
        m_streamKeys.reserve(m_payload.Results.size());
        for (auto const& payloadGroup : m_payload.Results)
        {
            std::vector<winrt::hstring> keys;
            keys.reserve(payloadGroup.Streams.size());
            for (std::size_t index{}; index < payloadGroup.Streams.size(); ++index)
            {
                keys.push_back(winrt::to_hstring(winrt::Windows::Foundation::GuidHelper::CreateNewGuid()));
            }
            m_streamKeys.push_back(std::move(keys));
        }

        // Applied before the failure is re-thrown on purpose. A provider outage is
        // exactly when the copy already on the device matters, and those files were
        // looked up for the parameters just passed in. The addon side of the state
        // is emptied by the same call, so nothing of the previous title survives.
        ApplyResolve();
        if (failure) std::rethrow_exception(failure);
    }

    void SourceService::ApplyResolve()
    {
        std::unordered_map<std::wstring, ResolvedSourceRecord> records;
        std::vector<winrt::hstring> orderedKeys;
        std::vector<winrt::hstring> localKeys;

        // Local records are built first so an addon stream offering the same release
        // can be folded into one of them instead of appearing a second time.
        if (m_parameters)
        {
            for (auto const& completed : m_downloads->CompletedFor(m_parameters.VideoId()))
            {
                Api::Dto::StreamRecord synthetic;
                synthetic.Filename = winrt::hstring{ completed.ReleaseName };
                if (completed.SizeBytes > 0) synthetic.VideoSize = completed.SizeBytes;

                // Derived from the job id rather than minted, so a rebuild leaves an
                // expanded row and the selection pointing at the same file.
                auto const key = winrt::hstring{ L"local:" + completed.JobId };
                ResolvedSourceRecord resolved{ key, L"", synthetic, ParseStreamInfo(synthetic), m_parameters };
                resolved.DownloadJobId = winrt::hstring{ completed.JobId };
                records.emplace(std::wstring{ key.c_str() }, std::move(resolved));
                orderedKeys.push_back(key);
                localKeys.push_back(key);
            }
        }

        std::vector<std::vector<winrt::hstring>> groupKeys;
        groupKeys.reserve(m_payload.Results.size());
        std::size_t sourceCount{};
        for (std::size_t groupIndex{}; groupIndex < m_payload.Results.size(); ++groupIndex)
        {
            auto const& payloadGroup = m_payload.Results[groupIndex];
            std::vector<winrt::hstring> keys;
            keys.reserve(payloadGroup.Streams.size());
            for (std::size_t streamIndex{}; streamIndex < payloadGroup.Streams.size(); ++streamIndex)
            {
                auto const& stream = payloadGroup.Streams[streamIndex];
                auto info = ParseStreamInfo(stream);

                // A stream that carried no name at all parses to a placeholder shared
                // by every other nameless stream. Folding one of those into a saved
                // file would claim the two are the same release when all they share
                // is the video, and would quietly play something else.
                auto const local = HasIdentifyingFilename(info)
                    ? std::find_if(
                        localKeys.begin(),
                        localKeys.end(),
                        [&records, &info](winrt::hstring const& localKey)
                        {
                            auto const& saved = records.at(std::wstring{ localKey.c_str() }).Info;
                            if (!HasIdentifyingFilename(saved)) return false;
                            return SameReleaseFile(
                                std::wstring_view{ saved.Filename.c_str(), saved.Filename.size() },
                                std::wstring_view{ info.Filename.c_str(), info.Filename.size() });
                        })
                    : localKeys.end();
                if (local != localKeys.end())
                {
                    // The copy on disk supersedes a stream of the same release. The
                    // first addon to offer it lends its URL and headers to the local
                    // record, so a file deleted outside the app can still be played.
                    auto& target = records.at(std::wstring{ local->c_str() });
                    if (target.Stream.Url.empty())
                    {
                        target.AddonId = payloadGroup.AddonId;
                        target.Stream.Url = stream.Url;
                        target.Stream.RequestHeaders = stream.RequestHeaders;
                        target.Stream.Subtitles = stream.Subtitles;
                        target.Stream.BingeGroup = stream.BingeGroup;
                        target.Stream.VideoHash = stream.VideoHash;
                    }
                    continue;
                }

                auto const key = m_streamKeys[groupIndex][streamIndex];
                records.emplace(
                    std::wstring{ key.c_str() },
                    ResolvedSourceRecord{ key, payloadGroup.AddonId, stream, std::move(info), m_parameters });
                orderedKeys.push_back(key);
                keys.push_back(key);
            }
            sourceCount += keys.size();
            groupKeys.push_back(std::move(keys));
        }

        // A rank is only meaningful once every addon has answered, so the records are
        // collected first, ranked across the whole resolve, and only then projected
        // into the display sources that carry it.
        auto rankedKeys = orderedKeys;
        std::stable_sort(
            rankedKeys.begin(),
            rankedKeys.end(),
            [&records](winrt::hstring const& left, winrt::hstring const& right)
            {
                auto const& first = records.at(std::wstring{ left.c_str() });
                auto const& second = records.at(std::wstring{ right.c_str() });
                // A file already on the device beats anything that has to come over
                // the network, whatever its quality: it starts instantly, costs no
                // bandwidth, and it was chosen once already by being saved.
                if (first.DownloadJobId.has_value() != second.DownloadJobId.has_value())
                {
                    return first.DownloadJobId.has_value();
                }
                return CompareStreams(first.Info, second.Info) < 0;
            });
        for (std::size_t position{}; position < rankedKeys.size(); ++position)
        {
            records.at(std::wstring{ rankedKeys[position].c_str() }).Rank =
                static_cast<std::int32_t>(position);
        }

        std::vector<winrt::HaloDesktop::SourceGroup> groups;
        groups.reserve(m_payload.Results.size() + m_payload.Errors.size());
        for (std::size_t index{}; index < groupKeys.size(); ++index)
        {
            std::vector<winrt::HaloDesktop::StreamSource> sources;
            sources.reserve(groupKeys[index].size());
            for (auto const& key : groupKeys[index])
            {
                sources.push_back(MakeDisplaySource(records.at(std::wstring{ key.c_str() }), false));
            }
            auto const groupSourceCount = sources.size();
            groups.push_back(winrt::make<winrt::HaloDesktop::implementation::SourceGroup>(
                m_payload.Results[index].AddonId,
                SanitizeAddonName(m_payload.Results[index].AddonName),
                L"RESOLVED",
                static_cast<std::int32_t>(groupSourceCount),
                winrt::single_threaded_vector(std::move(sources)).GetView(),
                true));
        }

        for (auto const& failed : m_payload.Errors)
        {
            groups.push_back(winrt::make<winrt::HaloDesktop::implementation::SourceGroup>(
                L"",
                SanitizeAddonName(failed.Name),
                AddonFailureCopy(failed.Code),
                0,
                winrt::single_threaded_vector<winrt::HaloDesktop::StreamSource>().GetView(),
                false));
        }

        std::vector<winrt::HaloDesktop::StreamSource> localSources;
        localSources.reserve(localKeys.size());
        for (auto const& key : localKeys)
        {
            localSources.push_back(MakeDisplaySource(records.at(std::wstring{ key.c_str() }), true));
        }

        m_records = std::move(records);
        m_orderedKeys = std::move(orderedKeys);
        m_groups = winrt::single_threaded_vector(std::move(groups)).GetView();
        m_localSources = winrt::single_threaded_vector(std::move(localSources)).GetView();
        m_summary = BuildResolveSummary(
            sourceCount,
            m_payload.Results.size(),
            m_payload.Errors.size(),
            localKeys.size(),
            m_elapsedSeconds);
    }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> SourceService::Groups() const
    {
        return m_groups;
    }


    winrt::HaloDesktop::PlaybackRequest SourceService::BuildPlaybackRequest(winrt::hstring const& key) const
    {
        auto const found = m_records.find(std::wstring(key.c_str()));
        if (found == m_records.end()) return nullptr;
        auto const& resolved = found->second;
        auto const& stream = resolved.Stream;
        auto const& navigation = resolved.Navigation;

        // A file on the device is played from the device. Going through the download
        // service rather than building the request here is what carries the download
        // id, and the offline sidecar subtitle and offline up-next both key off it.
        if (resolved.DownloadJobId)
        {
            if (auto const local = m_downloads->BuildPlaybackRequest(*resolved.DownloadJobId))
            {
                return local;
            }
            // The record survived but the file did not, which is what happens when it
            // was deleted outside the app. Streaming is only possible when an addon
            // offered the same release during this resolve.
            if (stream.Url.empty()) return nullptr;
        }

        return winrt::make<winrt::HaloDesktop::implementation::PlaybackRequest>(
            stream.Url,
            false,
            L"",
            L"",
            navigation.Type(),
            navigation.VideoId(),
            navigation.ItemId(),
            navigation.MetaId(),
            navigation.Title(),
            navigation.ShowName(),
            navigation.EpisodeLabel(),
            navigation.Poster(),
            resolved.AddonId,
            stream.BingeGroup.value_or(L""),
            resolved.Info.Filename,
            resolved.Info.SizeBytes.value_or(0),
            stream.VideoHash.value_or(L""),
            BuildSourceTagLine(resolved.Info),
            stream.RequestHeaders);
    }

    concurrency::task<DownloadStartOutcome> SourceService::StartDownloadAsync(
        winrt::hstring key,
        bool replaceExisting)
    {
        auto const version = m_requestVersion;
        auto const found = m_records.find(std::wstring{ key.c_str() });
        if (found == m_records.end())
        {
            co_return DownloadStartOutcome::Failed;
        }
        auto const resolved = found->second;
        // The sheet hides the affordance on a local row; this is the guard for a
        // stale key arriving after the row it belonged to was already saved.
        if (resolved.DownloadJobId)
        {
            co_return DownloadStartOutcome::AlreadyExists;
        }
        auto const& stream = resolved.Stream;
        auto const& navigation = resolved.Navigation;
        Downloads::DownloadStartRequest request{
            .Media = Downloads::DownloadMedia{
                .VideoId = std::wstring{ navigation.VideoId().c_str() },
                .ItemId = std::wstring{ navigation.ItemId().c_str() },
                .MediaType = std::wstring{ navigation.Type().c_str() },
                .MetaId = navigation.MetaId().empty()
                    ? std::nullopt
                    : std::optional<std::wstring>{ navigation.MetaId().c_str() },
                .Title = std::wstring{ navigation.Title().c_str() },
                .ShowName = navigation.ShowName().empty()
                    ? std::nullopt
                    : std::optional<std::wstring>{ navigation.ShowName().c_str() },
                .EpisodeLabel = navigation.EpisodeLabel().empty()
                    ? std::nullopt
                    : std::optional<std::wstring>{ navigation.EpisodeLabel().c_str() },
                .Poster = navigation.Poster().empty()
                    ? std::nullopt
                    : std::optional<std::wstring>{ navigation.Poster().c_str() },
                .AddonId = resolved.AddonId.empty()
                    ? std::nullopt
                    : std::optional<std::wstring>{ resolved.AddonId.c_str() },
                .BingeGroup = stream.BingeGroup
                    ? std::optional<std::wstring>{ stream.BingeGroup->c_str() }
                    : std::nullopt,
                .FileName = std::wstring{ resolved.Info.Filename.c_str() },
                .VideoSize = resolved.Info.SizeBytes,
                .VideoHash = stream.VideoHash
                    ? std::optional<std::wstring>{ stream.VideoHash->c_str() }
                    : std::nullopt,
                .StreamName = stream.Name
                    ? std::optional<std::wstring>{ stream.Name->c_str() }
                    : std::nullopt,
                .StreamTitle = stream.Title
                    ? std::optional<std::wstring>{ stream.Title->c_str() }
                    : std::nullopt,
            },
            .Request = Downloads::ProtectedRequest{
                .Url = std::wstring{ stream.Url.c_str() },
                .Headers = DownloadHeaders(stream.RequestHeaders),
            },
            .ReplaceExisting = replaceExisting,
        };

        try
        {
            auto const uiContext = winrt::apartment_context{};
            try { co_await m_settings->LoadAsync(); } catch (...) {}
            co_await uiContext;
            if (version != m_requestVersion)
            {
                co_return DownloadStartOutcome::Failed;
            }
            auto const preferred = m_settings->PreferredSubtitleLanguage();
            if (preferred)
            {
                request = co_await Downloads::PrepareDownloadWithOptionalSubtitleAsync(
                    std::move(request),
                    [this,
                     stream,
                     mediaType = navigation.Type(),
                     videoId = navigation.VideoId(),
                     preferredLanguage = *preferred]() mutable
                    {
                        return PrepareSubtitleAsync(
                            std::move(stream),
                            std::move(mediaType),
                            std::move(videoId),
                            std::move(preferredLanguage));
                    });
            }
            co_await uiContext;
            if (version != m_requestVersion)
            {
                co_return DownloadStartOutcome::Failed;
            }
            co_return co_await m_downloads->StartDownloadAsync(std::move(request));
        }
        catch (...)
        {
            co_return DownloadStartOutcome::Failed;
        }
    }

    concurrency::task<std::optional<Downloads::SubtitleRequest>> SourceService::PrepareSubtitleAsync(
        Api::Dto::StreamRecord stream,
        winrt::hstring mediaType,
        winrt::hstring videoId,
        winrt::hstring preferredLanguage)
    {
        std::optional<Api::Dto::SubtitleRecord> selected;
        for (auto const& subtitle : stream.Subtitles)
        {
            if (LanguageMatches(subtitle.Lang, preferredLanguage))
            {
                selected = Api::Dto::SubtitleRecord{ subtitle.Id, subtitle.Url, subtitle.Lang };
                break;
            }
        }

        if (!selected)
        {
            std::optional<winrt::hstring> hash;
            std::optional<std::uint64_t> size;
            if (stream.VideoHash && stream.VideoSize)
            {
                hash = stream.VideoHash;
                size = stream.VideoSize;
            }
            else
            {
                auto const computed = co_await m_api->ComputeVideoHashAsync(
                    stream.Url,
                    HashHeaders(stream.RequestHeaders));
                hash = computed.Hash;
                size = computed.Size;
            }

            auto const payload = co_await m_api->GetSubtitlesAsync(
                std::move(mediaType),
                std::move(videoId),
                hash,
                size,
                stream.Filename);
            for (auto const& group : payload.Results)
            {
                auto const match = std::find_if(
                    group.Subtitles.begin(),
                    group.Subtitles.end(),
                    [&preferredLanguage](auto const& subtitle)
                    {
                        return LanguageMatches(subtitle.Lang, preferredLanguage);
                    });
                if (match != group.Subtitles.end())
                {
                    selected = *match;
                    break;
                }
            }
        }

        if (!selected)
        {
            co_return std::nullopt;
        }

        auto const proxy = co_await m_api->BuildAddonProxyDownloadRequestAsync(selected->Url);
        co_return Downloads::SubtitleRequest{
            .Url = std::wstring{ proxy.Url.c_str() },
            .Language = std::wstring{ selected->Lang.c_str() },
            .Id = std::wstring{ selected->Id.c_str() },
            .Headers = DownloadHeaders(proxy.Headers),
        };
    }

    winrt::hstring SourceService::ResolveSummary() const { return m_summary; }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::StreamSource> SourceService::LocalSources() const
    {
        return m_localSources;
    }

    void SourceService::RefreshDownloadStates()
    {
        // A download finishing or being deleted changes which files exist locally,
        // not what the providers said, so the retained resolve is projected again
        // rather than requested again.
        ApplyResolve();
    }

    std::optional<ResolvedSourceRecord> SourceService::NativeRecord(winrt::hstring const& key) const
    {
        auto const found = m_records.find(std::wstring(key.c_str()));
        if (found == m_records.end()) return std::nullopt;
        return found->second;
    }

    void SourceService::OnAccountChanged() { ClearResolve(); }

    void SourceService::ClearResolve()
    {
        // The version bump is part of the clear: a resolve already in flight must not
        // be allowed to land on top of the emptied state.
        ++m_requestVersion;
        m_parameters = nullptr;
        m_payload = {};
        m_streamKeys.clear();
        m_elapsedSeconds = 0.0;
        m_records.clear();
        m_orderedKeys.clear();
        m_groups = winrt::single_threaded_vector<winrt::HaloDesktop::SourceGroup>().GetView();
        m_localSources = winrt::single_threaded_vector<winrt::HaloDesktop::StreamSource>().GetView();
        m_summary.clear();
    }

    winrt::hstring SanitizeAddonName(std::optional<winrt::hstring> const& name)
    {
        if (!name) return L"An addon";
        std::wstring cleaned;
        cleaned.reserve(name->size());
        for (auto const value : std::wstring_view(name->c_str(), name->size()))
        {
            if (value >= 0x20 && value != 0x7F) cleaned.push_back(value);
        }
        cleaned = Trim(std::move(cleaned));
        if (cleaned.empty() || cleaned.find(L"://") != std::wstring::npos || cleaned.find(L'/') != std::wstring::npos || cleaned.find(L'\\') != std::wstring::npos) return L"An addon";
        if (std::regex_search(cleaned, std::wregex(LR"([A-Za-z0-9_-]{32,})"))) return L"An addon";
        if (cleaned.size() > 80) cleaned.resize(80);
        return winrt::hstring{ cleaned };
    }

    // Sentence fragments rather than badges: the sources sheet reads them as
    // "<provider> did not answer in time", so they have to complete that sentence.
    winrt::hstring AddonFailureCopy(std::optional<winrt::hstring> const& code)
    {
        if (!code) return L"is unavailable";
        if (*code == L"timeout") return L"did not answer in time";
        if (*code == L"upstream_http") return L"returned an error";
        if (*code == L"blocked_target") return L"was blocked for safety";
        if (*code == L"invalid_response") return L"returned invalid data";
        return L"is unavailable";
    }
}
