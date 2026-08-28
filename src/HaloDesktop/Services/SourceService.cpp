#include "pch.h"
#include "Services/SourceService.h"

#include "Api/ApiClient.h"
#include "Models/Models.h"
#include "Security/ProtectedHttpHeaders.h"
#include "Services/Downloads/DownloadPreparation.h"
#include "Services/SettingsSyncService.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
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
          m_groups(winrt::single_threaded_vector<winrt::HaloDesktop::SourceGroup>().GetView())
    {
        if (!m_api || !m_downloads || !m_settings) throw std::invalid_argument("SourceService requires its dependencies.");
    }

    concurrency::task<void> SourceService::LoadAsync(winrt::HaloDesktop::SourcesNavParams const& parameters)
    {
        if (!parameters || parameters.Type().empty() || parameters.VideoId().empty())
        {
            throw std::invalid_argument("Source navigation parameters are incomplete.");
        }

        auto const version = ++m_requestVersion;
        auto const uiContext = winrt::apartment_context{};
        auto const started = std::chrono::steady_clock::now();
        auto payload = co_await m_api->GetStreamsAsync(parameters.Type(), parameters.VideoId());
        auto const elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        co_await uiContext;
        if (version != m_requestVersion) co_return;

        std::unordered_map<std::wstring, ResolvedSourceRecord> records;
        std::vector<winrt::hstring> orderedKeys;
        std::vector<winrt::HaloDesktop::SourceGroup> groups;
        std::size_t sourceCount{};
        auto const onDisk = m_downloads->HasCompleted(parameters.VideoId());

        // Two passes on purpose. A rank is only meaningful once every addon has
        // answered, so the records are collected first, ranked across the whole
        // resolve, and only then projected into the display sources that carry it.
        std::vector<std::vector<winrt::hstring>> groupKeys;
        groupKeys.reserve(payload.Results.size());
        for (auto const& payloadGroup : payload.Results)
        {
            std::vector<winrt::hstring> keys;
            keys.reserve(payloadGroup.Streams.size());
            for (auto const& stream : payloadGroup.Streams)
            {
                auto const key = winrt::to_hstring(winrt::Windows::Foundation::GuidHelper::CreateNewGuid());
                ResolvedSourceRecord resolved{ key, payloadGroup.AddonId, stream, ParseStreamInfo(stream), parameters };
                records.emplace(std::wstring(key.c_str()), std::move(resolved));
                orderedKeys.push_back(key);
                keys.push_back(key);
            }
            sourceCount += keys.size();
            groupKeys.push_back(std::move(keys));
        }

        auto rankedKeys = orderedKeys;
        std::stable_sort(
            rankedKeys.begin(),
            rankedKeys.end(),
            [&records](winrt::hstring const& left, winrt::hstring const& right)
            {
                return CompareStreams(
                    records.at(std::wstring{ left.c_str() }).Info,
                    records.at(std::wstring{ right.c_str() }).Info) < 0;
            });
        for (std::size_t position{}; position < rankedKeys.size(); ++position)
        {
            records.at(std::wstring{ rankedKeys[position].c_str() }).Rank =
                static_cast<std::int32_t>(position);
        }

        for (std::size_t index{}; index < groupKeys.size(); ++index)
        {
            std::vector<winrt::HaloDesktop::StreamSource> sources;
            sources.reserve(groupKeys[index].size());
            for (auto const& key : groupKeys[index])
            {
                sources.push_back(MakeDisplaySource(records.at(std::wstring{ key.c_str() }), onDisk));
            }
            auto const groupSourceCount = sources.size();
            groups.push_back(winrt::make<winrt::HaloDesktop::implementation::SourceGroup>(
                payload.Results[index].AddonId,
                SanitizeAddonName(payload.Results[index].AddonName),
                L"RESOLVED",
                static_cast<std::int32_t>(groupSourceCount),
                winrt::single_threaded_vector(std::move(sources)).GetView(),
                true));
        }

        for (auto const& failure : payload.Errors)
        {
            groups.push_back(winrt::make<winrt::HaloDesktop::implementation::SourceGroup>(
                L"",
                SanitizeAddonName(failure.Name),
                AddonFailureCopy(failure.Code),
                0,
                winrt::single_threaded_vector<winrt::HaloDesktop::StreamSource>().GetView(),
                false));
        }

        m_records = std::move(records);
        m_orderedKeys = std::move(orderedKeys);
        m_groups = winrt::single_threaded_vector(std::move(groups)).GetView();
        m_best = nullptr;
        if (!rankedKeys.empty())
        {
            m_best = MakeDisplaySource(m_records.at(std::wstring(rankedKeys.front().c_str())), onDisk);
        }

        // The ratio form ("3 of 4 providers") only appears when somebody failed;
        // naming it unconditionally would imply a problem on every healthy resolve.
        auto const providers = payload.Results.size() + payload.Errors.size();
        std::wostringstream summary;
        summary << sourceCount << (sourceCount == 1 ? L" source from " : L" sources from ");
        if (!payload.Errors.empty()) summary << payload.Results.size() << L" of ";
        summary << providers << (providers == 1 ? L" provider, found in " : L" providers, found in ")
                << std::fixed << std::setprecision(1) << elapsed << L" seconds";
        m_summary = winrt::hstring{ summary.str() };
    }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::SourceGroup> SourceService::Groups() const
    {
        return m_groups;
    }

    winrt::Windows::Foundation::Collections::IVectorView<winrt::HaloDesktop::StreamSource> SourceService::Filter(winrt::hstring const& quality) const
    {
        std::vector<winrt::HaloDesktop::StreamSource> result;
        for (auto const& group : m_groups)
        {
            for (auto const& source : group.Sources()) if (Matches(source, quality)) result.push_back(source);
        }
        return winrt::single_threaded_vector(std::move(result)).GetView();
    }

    winrt::HaloDesktop::StreamSource SourceService::BestSource() const { return m_best; }

    winrt::HaloDesktop::PlaybackRequest SourceService::BuildPlaybackRequest(winrt::hstring const& key) const
    {
        auto const found = m_records.find(std::wstring(key.c_str()));
        if (found == m_records.end()) return nullptr;
        auto const& resolved = found->second;
        auto const& stream = resolved.Stream;
        auto const& navigation = resolved.Navigation;
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

    std::int32_t SourceService::Count(winrt::hstring const& filter) const noexcept
    {
        std::int32_t count{};
        for (auto const& group : m_groups)
        {
            for (auto const& source : group.Sources()) if (Matches(source, filter)) ++count;
        }
        return count;
    }

    void SourceService::RefreshDownloadStates()
    {
        std::vector<winrt::HaloDesktop::SourceGroup> groups;
        groups.reserve(m_groups.Size());
        for (auto const& group : m_groups)
        {
            std::vector<winrt::HaloDesktop::StreamSource> sources;
            sources.reserve(group.Sources().Size());
            for (auto const& source : group.Sources())
            {
                auto const found = m_records.find(std::wstring{ source.Key().c_str() });
                if (found != m_records.end())
                {
                    sources.push_back(MakeDisplaySource(
                        found->second,
                        m_downloads->HasCompleted(found->second.Navigation.VideoId())));
                }
            }
            groups.push_back(winrt::make<winrt::HaloDesktop::implementation::SourceGroup>(
                group.AddonId(),
                group.Name(),
                group.Note(),
                group.Count(),
                winrt::single_threaded_vector(std::move(sources)).GetView(),
                group.Answered()));
        }
        m_groups = winrt::single_threaded_vector(std::move(groups)).GetView();
        if (m_best)
        {
            auto const found = m_records.find(std::wstring{ m_best.Key().c_str() });
            if (found != m_records.end())
            {
                m_best = MakeDisplaySource(
                    found->second,
                    m_downloads->HasCompleted(found->second.Navigation.VideoId()));
            }
        }
    }

    std::optional<ResolvedSourceRecord> SourceService::NativeRecord(winrt::hstring const& key) const
    {
        auto const found = m_records.find(std::wstring(key.c_str()));
        if (found == m_records.end()) return std::nullopt;
        return found->second;
    }

    void SourceService::OnAccountChanged()
    {
        ++m_requestVersion;
        m_records.clear();
        m_orderedKeys.clear();
        m_groups = winrt::single_threaded_vector<winrt::HaloDesktop::SourceGroup>().GetView();
        m_best = nullptr;
        m_summary.clear();
    }

    bool SourceService::Matches(winrt::HaloDesktop::StreamSource const& source, winrt::hstring const& filter) const noexcept
    {
        if (filter.empty() || filter == L"All") return true;
        if (filter == L"Instant") return source.Status() == winrt::HaloDesktop::StreamStatus::Instant || source.Status() == winrt::HaloDesktop::StreamStatus::OnDisk;
        return source.Quality() == filter;
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
