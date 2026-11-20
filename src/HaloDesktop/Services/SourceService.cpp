#include "pch.h"
#include "Services/SourceService.h"

#include "Api/ApiClient.h"
#include "Api/BackendSelector.h"
#include "Api/HaloBackend.h"
#include "Models/Models.h"
#include "Security/ProtectedHttpHeaders.h"
#include "Services/Downloads/DownloadPreparation.h"
#include "Services/LanguagePolicy.h"
#include "Services/SearchResultsPolicy.h"
#include "Services/SettingsSyncService.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <map>
#include <stdexcept>
#include <utility>
#include <winrt/Windows.Foundation.h>

namespace
{
    // How long a finished stream request may answer a later resolve of the same
    // video. Long enough for "open the title, then open its sources" to cost one
    // request; short enough that a sheet opened much later asks the addons again,
    // as cached or debrid availability moves.
    constexpr auto StreamAnswerReuse = std::chrono::minutes{ 5 };

    // An empty preference names no language, so it matches nothing.
    bool LanguageMatches(winrt::hstring const& subtitleLanguage, winrt::hstring const& preferred)
    {
        return !preferred.empty()
            && HaloDesktop::Services::LanguageTagsMatch(subtitleLanguage, preferred);
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

    std::optional<std::wstring> OptionalText(winrt::hstring const& value)
    {
        return value.empty() ? std::nullopt : std::optional<std::wstring>{ value.c_str() };
    }

    std::optional<std::wstring> OptionalText(std::optional<winrt::hstring> const& value)
    {
        return value ? std::optional<std::wstring>{ value->c_str() } : std::nullopt;
    }
}

namespace HaloDesktop::Services
{
    SourceService::SourceService(
        std::shared_ptr<Api::ApiClient> api,
        std::shared_ptr<Api::BackendSelector> backends,
        std::shared_ptr<IDownloadService> downloads,
        std::shared_ptr<SettingsSyncService> settings)
        : m_api(std::move(api)), m_backends(std::move(backends)), m_downloads(std::move(downloads)), m_settings(std::move(settings)),
          m_current(std::make_shared<Sources::ParsedResolve const>())
    {
        if (!m_api || !m_backends || !m_downloads || !m_settings) throw std::invalid_argument("SourceService requires its dependencies.");
    }

    concurrency::task<void> SourceService::LoadAsync(
        winrt::HaloDesktop::SourcesNavParams const& parameters,
        StreamReuse reuse)
    {
        if (!parameters || parameters.Type().empty() || parameters.VideoId().empty())
        {
            ClearResolve();
            throw std::invalid_argument("Source navigation parameters are incomplete.");
        }

        auto const version = ++m_requestVersion;
        auto const uiContext = winrt::apartment_context{};
        // The previous title's answer is dropped before this one is asked for, so
        // nothing of it can be played or saved under this title's name meanwhile.
        Adopt(nullptr, std::make_shared<Sources::ParsedResolve const>());

        std::optional<StreamFetch> fetched;
        std::exception_ptr failure;
        try
        {
            fetched = co_await SharedStreams(parameters.Type(), parameters.VideoId(), reuse);
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        co_await uiContext;
        if (version != m_requestVersion) co_return;
        if (failure) std::rethrow_exception(failure);

        // Every release name is parsed off the UI thread. The keys are minted once
        // per resolve, so a rebuild never renumbers the rows under an open sheet.
        auto payload = std::move(fetched->Payload);
        co_await winrt::resume_background();
        auto parsed = std::make_shared<Sources::ParsedResolve const>(Sources::ParseResolve(
            payload,
            [](std::size_t, std::size_t)
            {
                return winrt::to_hstring(winrt::Windows::Foundation::GuidHelper::CreateNewGuid());
            }));
        co_await uiContext;
        if (version != m_requestVersion) co_return;
        Adopt(parameters, std::move(parsed));
    }

    concurrency::task<StreamFetch> SourceService::RequestStreamsAsync(winrt::hstring type, winrt::hstring videoId)
    {
        if (type.empty() || videoId.empty())
        {
            throw std::invalid_argument("A stream request needs a type and a video.");
        }
        return SharedStreams(type, videoId, StreamReuse::Recent);
    }

    concurrency::task<StreamFetch> SourceService::SharedStreams(
        winrt::hstring const& type,
        winrt::hstring const& videoId,
        StreamReuse reuse)
    {
        if (reuse == StreamReuse::Recent && m_fetch && m_fetch->Task
            && m_fetch->Type == type && m_fetch->VideoId == videoId
            && (!m_fetch->AnsweredAt || std::chrono::steady_clock::now() - *m_fetch->AnsweredAt < StreamAnswerReuse))
        {
            return *m_fetch->Task;
        }
        // The entry is in place before the request starts, so a request that
        // completes before this returns still finds it by its serial.
        auto const serial = ++m_fetchSerial;
        m_fetch = SharedFetch{ type, videoId, std::nullopt, std::nullopt, serial };
        auto task = FetchStreamsAsync(type, videoId, serial);
        if (m_fetch && m_fetch->Serial == serial)
        {
            m_fetch->Task = task;
        }
        return task;
    }

    concurrency::task<StreamFetch> SourceService::FetchStreamsAsync(
        winrt::hstring type,
        winrt::hstring videoId,
        std::uint64_t serial)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const started = std::chrono::steady_clock::now();
        std::optional<Api::Dto::StreamsPayload> payload;
        std::exception_ptr failure;
        try
        {
            payload = co_await m_backends->Current()->GetStreamsAsync(type, videoId);
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        auto const elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        co_await uiContext;
        if (m_fetch && m_fetch->Serial == serial)
        {
            if (failure)
            {
                m_fetch.reset();
            }
            else
            {
                m_fetch->AnsweredAt = std::chrono::steady_clock::now();
            }
        }
        if (failure) std::rethrow_exception(failure);
        co_return StreamFetch{ std::move(*payload), elapsed };
    }

    void SourceService::Adopt(
        winrt::HaloDesktop::SourcesNavParams parameters,
        std::shared_ptr<Sources::ParsedResolve const> parsed)
    {
        m_index.clear();
        for (std::size_t group{}; group < parsed->Groups.size(); ++group)
        {
            auto const& streams = parsed->Groups[group].Streams;
            for (std::size_t index{}; index < streams.size(); ++index)
            {
                m_index.emplace(std::wstring{ streams[index].Key.c_str() }, std::pair{ group, index });
            }
        }
        m_parameters = std::move(parameters);
        m_current = std::move(parsed);
    }

    std::shared_ptr<Sources::ParsedResolve const> SourceService::Current() const
    {
        return m_current;
    }

    Sources::ParsedStream const* SourceService::Find(winrt::hstring const& key, winrt::hstring* addonId) const
    {
        if (!m_parameters) return nullptr;
        auto const found = m_index.find(std::wstring{ key.c_str() });
        if (found == m_index.end()) return nullptr;
        auto const& group = m_current->Groups[found->second.first];
        auto const& stream = group.Streams[found->second.second];
        if (stream.Stream.Url.empty()) return nullptr;
        if (addonId) *addonId = group.AddonId;
        return &stream;
    }

    winrt::HaloDesktop::PlaybackRequest SourceService::BuildPlaybackRequest(winrt::hstring const& key) const
    {
        winrt::hstring addonId;
        auto const* resolved = Find(key, &addonId);
        if (!resolved) return nullptr;
        auto const& stream = resolved->Stream;
        auto const& navigation = m_parameters;

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
            addonId,
            stream.BingeGroup.value_or(L""),
            resolved->Info.Filename,
            resolved->Info.SizeBytes.value_or(0),
            stream.VideoHash.value_or(L""),
            stream.RequestHeaders);
    }

    concurrency::task<DownloadStartResult> SourceService::StartDownloadAsync(
        winrt::hstring key,
        bool replaceExisting)
    {
        auto const version = m_requestVersion;
        winrt::hstring addonId;
        auto const* found = Find(key, &addonId);
        if (!found)
        {
            co_return DownloadStartResult{};
        }
        // Copied before the first suspension: a resolve landing meanwhile replaces
        // what the pointer looks at.
        auto const stream = found->Stream;
        auto const info = found->Info;
        auto const navigation = m_parameters;
        Downloads::DownloadStartRequest request{
            .Media = Downloads::DownloadMedia{
                .VideoId = std::wstring{ navigation.VideoId().c_str() },
                .ItemId = std::wstring{ navigation.ItemId().c_str() },
                .MediaType = std::wstring{ navigation.Type().c_str() },
                .MetaId = OptionalText(navigation.MetaId()),
                .Title = std::wstring{ navigation.Title().c_str() },
                .ShowName = OptionalText(navigation.ShowName()),
                .EpisodeLabel = OptionalText(navigation.EpisodeLabel()),
                .Poster = OptionalText(navigation.Poster()),
                .AddonId = OptionalText(addonId),
                .BingeGroup = OptionalText(stream.BingeGroup),
                .FileName = std::wstring{ info.Filename.c_str() },
                .VideoSize = info.SizeBytes,
                .VideoHash = OptionalText(stream.VideoHash),
                .StreamName = OptionalText(stream.Name),
                .StreamTitle = OptionalText(stream.Title),
                .DynamicRange = OptionalText(info.DynamicRange),
                .Year = ReleaseYear(OptionalText(navigation.ReleaseInfo())),
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
                co_return DownloadStartResult{};
            }
            // This app resolves the preferred subtitle before the start, so the
            // download carries it from its first byte.
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
                co_return DownloadStartResult{};
            }
            co_return co_await m_downloads->StartDownloadAsync(std::move(request));
        }
        catch (...)
        {
            co_return DownloadStartResult{};
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

            auto const payload = co_await m_backends->Current()->GetSubtitlesAsync(
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

        if (m_backends->UsesDevice())
        {
            // Without an account there is no addon proxy: the engine fetches
            // the addon's own URL, with no headers, as the reference's download
            // does in either mode.
            co_return Downloads::SubtitleRequest{
                .Url = std::wstring{ selected->Url.c_str() },
                .Language = std::wstring{ selected->Lang.c_str() },
                .Id = std::wstring{ selected->Id.c_str() },
                .Headers = {},
            };
        }
        auto const proxy = co_await m_api->BuildAddonProxyDownloadRequestAsync(selected->Url);
        co_return Downloads::SubtitleRequest{
            .Url = std::wstring{ proxy.Url.c_str() },
            .Language = std::wstring{ selected->Lang.c_str() },
            .Id = std::wstring{ selected->Id.c_str() },
            .Headers = DownloadHeaders(proxy.Headers),
        };
    }

    void SourceService::OnAccountChanged() { ClearResolve(); }

    void SourceService::ClearResolve()
    {
        // The version bump is part of the clear: a resolve already in flight must not
        // be allowed to land on top of the emptied state.
        ++m_requestVersion;
        // The shared request goes too: an answer from before an account change
        // must not be adopted after it.
        ++m_fetchSerial;
        m_fetch.reset();
        Adopt(nullptr, std::make_shared<Sources::ParsedResolve const>());
    }
}
