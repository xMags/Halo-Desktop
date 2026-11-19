#include "pch.h"
#include "Api/ApiClient.h"
#include <winrt/Windows.Web.Http.Headers.h>
#include "Api/ApiError.h"
#include "Api/BoundedHttpContent.h"
#include "Api/HttpExecutor.h"
#include "Services/Auth/ITokenProvider.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <winrt/Windows.Web.Http.h>

namespace
{
    winrt::hstring NormalizeBaseUrl(winrt::hstring const& input)
    {
        std::wstring baseUrl{ input };
        while (!baseUrl.empty() && baseUrl.back() == L'/')
        {
            baseUrl.pop_back();
        }
        if (baseUrl.empty())
        {
            throw std::invalid_argument{ "The server base URL is empty." };
        }

        winrt::Windows::Foundation::Uri const uri{ baseUrl };
        auto const scheme = uri.SchemeName();
        if ((scheme != L"http" && scheme != L"https") || uri.Host().empty())
        {
            throw std::invalid_argument{ "The server base URL must be an absolute HTTP or HTTPS URL." };
        }
        return winrt::hstring{ baseUrl };
    }

    winrt::hstring EncodeUriComponent(winrt::hstring const& input)
    {
        constexpr char hex[] = "0123456789ABCDEF";
        auto const utf8 = winrt::to_string(input);
        std::string result;
        for (auto const character : utf8)
        {
            auto const byte = static_cast<std::uint8_t>(character);
            auto const unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
                || (byte >= '0' && byte <= '9') || byte == '-' || byte == '_'
                || byte == '.' || byte == '!' || byte == '~' || byte == '*'
                || byte == '\'' || byte == '(' || byte == ')';
            if (unreserved)
            {
                result.push_back(static_cast<char>(byte));
            }
            else
            {
                result.push_back('%');
                result.push_back(hex[byte >> 4]);
                result.push_back(hex[byte & 15]);
            }
        }
        return winrt::to_hstring(result);
    }

    winrt::hstring TrimmedText(winrt::hstring const& input)
    {
        std::wstring_view text{ input };
        auto const isSpace = [](wchar_t character) { return std::iswspace(character) != 0; };
        while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
        while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
        return winrt::hstring{ text };
    }

    bool AllDigits(std::wstring_view text) noexcept
    {
        return !text.empty() && std::all_of(text.begin(), text.end(), [](wchar_t character)
        {
            return character >= L'0' && character <= L'9';
        });
    }

    // The ids the lookup endpoint takes, in the shapes it checks: a TVDB
    // number, or an IMDb title or person id. Nothing else is ever put in its URL.
    bool IsLookupId(winrt::hstring const& source, winrt::hstring const& id)
    {
        std::wstring_view const text{ id };
        if (source == L"tvdb") return text.size() <= 10 && AllDigits(text);
        if (source != L"imdb" || text.size() < 3 || text.size() > 12) return false;
        return (text.starts_with(L"tt") || text.starts_with(L"nm")) && AllDigits(text.substr(2));
    }

    bool IsLowerHex(std::wstring_view text) noexcept
    {
        return std::all_of(text.begin(), text.end(), [](wchar_t character)
        {
            return (character >= L'0' && character <= L'9') || (character >= L'a' && character <= L'f');
        });
    }

    // The ids the server mints for magnet requests (`mg_` and plain
    // characters); nothing else is ever put in a magnet URL.
    bool IsMagnetId(winrt::hstring const& id)
    {
        std::wstring_view const text{ id };
        if (text.size() < 4 || text.size() > 67 || !text.starts_with(L"mg_")) return false;
        return std::all_of(text.begin() + 3, text.end(), [](wchar_t character)
        {
            return (character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z')
                || (character >= L'0' && character <= L'9') || character == L'_' || character == L'-';
        });
    }
}

namespace HaloDesktop::Api
{
    ApiClient::ApiClient(
        winrt::hstring baseUrl,
        std::shared_ptr<HttpExecutor> executor,
        std::shared_ptr<::HaloDesktop::Services::Auth::ITokenProvider> tokenProvider)
        : m_baseUrl(NormalizeBaseUrl(baseUrl)),
          m_executor(std::move(executor)),
          m_tokenProvider(std::move(tokenProvider))
    {
        if (!m_executor || !m_tokenProvider)
        {
            throw std::invalid_argument{ "ApiClient requires HTTP and token-provider dependencies." };
        }
    }

    winrt::hstring ApiClient::BaseUrl() const
    {
        return m_baseUrl;
    }

    winrt::hstring ApiClient::ServerFor(::HaloDesktop::Services::Auth::SessionTarget const& target) const
    {
        return target.ServerUrl && !target.ServerUrl->empty() ? *target.ServerUrl : m_baseUrl;
    }

    void ApiClient::RequireSameTarget(::HaloDesktop::Services::Auth::SessionTarget const& target) const
    {
        if (m_tokenProvider->CurrentTarget() != target)
        {
            throw winrt::hresult_canceled{};
        }
    }

    concurrency::task<Dto::HealthStatus> ApiClient::GetHealthAsync()
    {
        // The server the app talks to now: a self-hosted session's own.
        auto const server = ServerFor(m_tokenProvider->CurrentTarget());
        co_await winrt::resume_background();
        auto const started = std::chrono::steady_clock::now();
        auto const value = co_await m_executor->SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            Endpoint(server, L"/health"));
        auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        co_return Dto::HealthStatus{
            .Ok = Mappers::ParseHealth(value),
            .RoundTrip = elapsed,
        };
    }

    concurrency::task<Dto::AuthConfig> ApiClient::GetAuthConfigAsync()
    {
        co_await winrt::resume_background();
        auto const value = co_await m_executor->SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            Endpoint(m_baseUrl, L"/auth/config"));
        co_return Mappers::ParseAuthConfig(value);
    }

    concurrency::task<Dto::AuthConfig> ApiClient::GetAuthConfigAtAsync(winrt::hstring serverUrl)
    {
        auto const server = NormalizeBaseUrl(serverUrl);
        co_await winrt::resume_background();
        auto const value = co_await m_executor->SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            Endpoint(server, L"/auth/config"));
        co_return Mappers::ParseAuthConfig(value);
    }

    concurrency::task<Dto::Me> ApiClient::GetMeAsync()
    {
        co_await winrt::resume_background();
        auto const value = co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            L"/auth/me");
        co_return Mappers::ParseMe(value);
    }

    concurrency::task<std::optional<std::vector<std::uint8_t>>> ApiClient::GetAvatarAsync()
    {
        // The same token and refresh policy as SendAuthenticatedJsonAsync, for
        // a body that is an image rather than JSON.
        auto const target = m_tokenProvider->CurrentTarget();
        auto const generation = target.Generation;
        co_await winrt::resume_background();
        RequireSameTarget(target);
        auto token = co_await m_tokenProvider->AccessTokenAsync();
        RequireSameTarget(target);
        if (!token)
        {
            co_await m_tokenProvider->RejectSessionAsync(generation);
            ThrowSessionRejected();
        }

        auto const uri = Endpoint(ServerFor(target), L"/auth/avatar");
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            winrt::Windows::Web::Http::HttpRequestMessage request{ winrt::Windows::Web::Http::HttpMethod::Get(), uri };
            request.Headers().TryAppendWithoutValidation(L"Authorization", winrt::hstring{ L"Bearer " } + *token);
            auto const response = co_await m_executor->SendForStreamAsync(request);
            RequireSameTarget(target);
            auto const status = static_cast<std::uint16_t>(response.StatusCode());
            // No picture, or a server that keeps none.
            if (status == 404) co_return std::nullopt;
            if (status >= 200 && status < 300)
            {
                co_return co_await ReadBoundedBytesAsync(response.Content(), MaximumAvatarBytes);
            }
            // A second 401 follows a successful refresh, so it fails this
            // request without ending the session.
            if (status != 401 || attempt == 1)
            {
                throw winrt::hresult_error{ ApiError::MakeHttpStatus(status), L"The account picture could not be loaded." };
            }
            token = co_await m_tokenProvider->RefreshAccessTokenAsync();
            RequireSameTarget(target);
            if (!token)
            {
                co_await m_tokenProvider->RejectSessionAsync(generation);
                ThrowSessionRejected();
            }
        }
        throw winrt::hresult_error{ ApiError::SessionRejected };
    }

    concurrency::task<Dto::AddonsPayload> ApiClient::GetAddonsAsync()
    {
        auto const value = co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            L"/addons");
        co_return Mappers::ParseAddons(value);
    }

    concurrency::task<void> ApiClient::PutAddonsAsync(std::vector<winrt::hstring> transportUrls)
    {
        return PutAddonsAsync(std::move(transportUrls), false);
    }

    concurrency::task<void> ApiClient::PatchAddonAsync(winrt::hstring addonId, bool hideCatalogs)
    {
        return PatchAddonAsync(std::move(addonId), false, hideCatalogs);
    }

    concurrency::task<void> ApiClient::PutAddonsAsync(
        std::vector<winrt::hstring> transportUrls,
        bool global)
    {
        winrt::Windows::Data::Json::JsonArray body;
        for (auto const& url : transportUrls)
        {
            body.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue(url));
        }
        static_cast<void>(co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Put(),
            global ? L"/addons/global" : L"/addons",
            body.Stringify()));
    }

    concurrency::task<void> ApiClient::PatchAddonAsync(
        winrt::hstring addonId,
        bool global,
        bool hideCatalogs)
    {
        winrt::Windows::Data::Json::JsonObject body;
        body.Insert(L"hideCatalogs", winrt::Windows::Data::Json::JsonValue::CreateBooleanValue(hideCatalogs));
        auto const prefix = global ? winrt::hstring{ L"/addons/global/" } : winrt::hstring{ L"/addons/" };
        auto const path = prefix + EncodeUriComponent(addonId);
        static_cast<void>(co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod{ L"PATCH" },
            path.c_str(),
            body.Stringify()));
    }

    concurrency::task<Dto::SettingsPayload> ApiClient::GetSettingsAsync()
    {
        auto const value = co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            L"/settings");
        co_return Mappers::ParseSettings(value);
    }

    concurrency::task<Dto::SettingsPayload> ApiClient::PutSettingsAsync(
        winrt::Windows::Data::Json::JsonObject value,
        std::int64_t updatedAt)
    {
        winrt::Windows::Data::Json::JsonObject body;
        body.Insert(L"value", value);
        body.Insert(L"updatedAt", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(
            static_cast<double>(updatedAt)));
        auto const response = co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Put(),
            L"/settings",
            body.Stringify());
        co_return Mappers::ParseSettings(response);
    }

    concurrency::task<std::vector<Dto::MetaPreview>> ApiClient::GetCatalogAsync(
        winrt::hstring addonId,
        winrt::hstring type,
        winrt::hstring catalogId,
        std::vector<std::pair<winrt::hstring, winrt::hstring>> extras)
    {
        std::wstring path = L"/catalog?addon=" + std::wstring{ EncodeUriComponent(addonId) }
            + L"&type=" + std::wstring{ EncodeUriComponent(type) }
            + L"&id=" + std::wstring{ EncodeUriComponent(catalogId) };
        for (auto const& [name, value] : extras)
        {
            path.append(L"&").append(EncodeUriComponent(name)).append(L"=").append(EncodeUriComponent(value));
        }
        auto const response = co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(), path.c_str());
        co_return Mappers::ParseCatalog(response);
    }

    concurrency::task<std::vector<Dto::LibraryRow>> ApiClient::GetLibraryAsync()
    {
        auto const response = co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(), L"/library");
        co_return Mappers::ParseLibrary(response);
    }

    concurrency::task<std::vector<Dto::WatchEntry>> ApiClient::GetWatchStateAsync()
    {
        auto const response = co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(), L"/watch-state");
        co_return Mappers::ParseWatchState(response);
    }
    concurrency::task<std::vector<Dto::WatchEntry>> ApiClient::PutWatchStateAsync(std::vector<Dto::WatchEntry> rows)
    {
        winrt::Windows::Data::Json::JsonArray body;
        for(auto const&row:rows){winrt::Windows::Data::Json::JsonObject object;object.Insert(L"videoId",winrt::Windows::Data::Json::JsonValue::CreateStringValue(row.VideoId));object.Insert(L"itemId",winrt::Windows::Data::Json::JsonValue::CreateStringValue(row.ItemId));object.Insert(L"positionSec",winrt::Windows::Data::Json::JsonValue::CreateNumberValue(row.PositionSec));object.Insert(L"durationSec",winrt::Windows::Data::Json::JsonValue::CreateNumberValue(row.DurationSec));object.Insert(L"watched",winrt::Windows::Data::Json::JsonValue::CreateBooleanValue(row.Watched));if(row.Name)object.Insert(L"name",winrt::Windows::Data::Json::JsonValue::CreateStringValue(*row.Name));if(row.Poster)object.Insert(L"poster",winrt::Windows::Data::Json::JsonValue::CreateStringValue(*row.Poster));if(row.RemovedAt)object.Insert(L"removedAt",winrt::Windows::Data::Json::JsonValue::CreateNumberValue(static_cast<double>(*row.RemovedAt)));object.Insert(L"updatedAt",winrt::Windows::Data::Json::JsonValue::CreateNumberValue(static_cast<double>(row.UpdatedAt)));body.Append(object);}
        auto const response=co_await SendAuthenticatedJsonAsync(winrt::Windows::Web::Http::HttpMethod::Put(),L"/watch-state",body.Stringify());
        co_return Mappers::ParseWatchState(response);
    }

    concurrency::task<Dto::MetaDetail> ApiClient::GetMetaAsync(winrt::hstring type,winrt::hstring metaId)
    {
        auto const path=winrt::hstring{L"/meta?type="}+EncodeUriComponent(type)+L"&id="+EncodeUriComponent(metaId);
        auto const response=co_await SendAuthenticatedJsonAsync(winrt::Windows::Web::Http::HttpMethod::Get(),path.c_str());co_return Mappers::ParseMeta(response);
    }

    concurrency::task<std::vector<Dto::LibraryRow>> ApiClient::PutLibraryAsync(std::vector<Dto::LibraryRow> rows)
    {
        winrt::Windows::Data::Json::JsonArray body;
        for(auto const&r:rows){winrt::Windows::Data::Json::JsonObject o;o.Insert(L"id",winrt::Windows::Data::Json::JsonValue::CreateStringValue(r.Id));o.Insert(L"type",winrt::Windows::Data::Json::JsonValue::CreateStringValue(r.Type));o.Insert(L"name",winrt::Windows::Data::Json::JsonValue::CreateStringValue(r.Name));if(r.Poster)o.Insert(L"poster",winrt::Windows::Data::Json::JsonValue::CreateStringValue(*r.Poster));o.Insert(L"addedAt",winrt::Windows::Data::Json::JsonValue::CreateNumberValue(static_cast<double>(r.AddedAt)));if(r.RemovedAt)o.Insert(L"removedAt",winrt::Windows::Data::Json::JsonValue::CreateNumberValue(static_cast<double>(*r.RemovedAt)));o.Insert(L"updatedAt",winrt::Windows::Data::Json::JsonValue::CreateNumberValue(static_cast<double>(r.UpdatedAt)));body.Append(o);}
        auto const response=co_await SendAuthenticatedJsonAsync(winrt::Windows::Web::Http::HttpMethod::Put(),L"/library",body.Stringify());co_return Mappers::ParseLibrary(response);
    }
    concurrency::task<Dto::StreamsPayload> ApiClient::GetStreamsAsync(winrt::hstring type,winrt::hstring videoId){auto path=winrt::hstring{L"/streams?type="}+EncodeUriComponent(type)+L"&videoId="+EncodeUriComponent(videoId);auto response=co_await SendAuthenticatedJsonAsync(winrt::Windows::Web::Http::HttpMethod::Get(),path.c_str());co_return Mappers::ParseStreams(response);}
    concurrency::task<Dto::NextEpisodePayload> ApiClient::GetNextEpisodeAsync(winrt::hstring type,winrt::hstring metaId,winrt::hstring videoId,winrt::hstring addonId,winrt::hstring bingeGroup)
    {
        auto path=winrt::hstring{L"/next-episode?type="}+EncodeUriComponent(type)+L"&metaId="+EncodeUriComponent(metaId)+L"&videoId="+EncodeUriComponent(videoId);
        if(!addonId.empty())path=path+L"&addon="+EncodeUriComponent(addonId);
        if(!bingeGroup.empty())path=path+L"&bingeGroup="+EncodeUriComponent(bingeGroup);
        auto response=co_await SendAuthenticatedJsonAsync(winrt::Windows::Web::Http::HttpMethod::Get(),path.c_str());
        co_return Mappers::ParseNextEpisode(response);
    }
    concurrency::task<Dto::SubtitlesPayload> ApiClient::GetSubtitlesAsync(winrt::hstring type,winrt::hstring videoId,std::optional<winrt::hstring> videoHash,std::optional<std::uint64_t> videoSize,std::optional<winrt::hstring> filename){auto path=winrt::hstring{L"/subtitles?type="}+EncodeUriComponent(type)+L"&videoId="+EncodeUriComponent(videoId);if(videoHash)path=path+L"&videoHash="+EncodeUriComponent(*videoHash);if(videoSize)path=path+L"&videoSize="+winrt::to_hstring(*videoSize);if(filename)path=path+L"&filename="+EncodeUriComponent(*filename);auto response=co_await SendAuthenticatedJsonAsync(winrt::Windows::Web::Http::HttpMethod::Get(),path.c_str());co_return Mappers::ParseSubtitles(response);}

    concurrency::task<winrt::Windows::Web::Http::HttpResponseMessage> ApiClient::OpenAddonProxyAsync(winrt::hstring targetUrl)
    {
        auto const target=m_tokenProvider->CurrentTarget();auto const generation=target.Generation;co_await winrt::resume_background();RequireSameTarget(target);auto token=co_await m_tokenProvider->AccessTokenAsync();RequireSameTarget(target);if(!token){co_await m_tokenProvider->RejectSessionAsync(generation);ThrowSessionRejected();}
        auto const uri=Endpoint(ServerFor(target),(winrt::hstring{L"/addon-proxy?url="}+EncodeUriComponent(targetUrl)).c_str());
        for(int attempt=0;attempt<2;++attempt){winrt::Windows::Web::Http::HttpRequestMessage request{winrt::Windows::Web::Http::HttpMethod::Get(),uri};request.Headers().TryAppendWithoutValidation(L"Authorization",winrt::hstring{L"Bearer "}+*token);auto response=co_await m_executor->SendForStreamAsync(request);RequireSameTarget(target);auto const status=static_cast<std::uint16_t>(response.StatusCode());if(status>=200&&status<300)co_return response;if(status!=401||attempt==1)throw winrt::hresult_error{ApiError::MakeHttpStatus(status),L"The subtitle proxy request failed."};token=co_await m_tokenProvider->RefreshAccessTokenAsync();RequireSameTarget(target);if(!token){co_await m_tokenProvider->RejectSessionAsync(generation);ThrowSessionRejected();}}
        throw winrt::hresult_error{ApiError::SessionRejected};
    }
    concurrency::task<AuthenticatedDownloadRequest> ApiClient::BuildAddonProxyDownloadRequestAsync(
        winrt::hstring targetUrl)
    {
        auto const target = m_tokenProvider->CurrentTarget();
        auto const generation = target.Generation;
        co_await winrt::resume_background();
        RequireSameTarget(target);
        auto token = co_await m_tokenProvider->AccessTokenAsync();
        RequireSameTarget(target);
        if (!token)
        {
            co_await m_tokenProvider->RejectSessionAsync(generation);
            ThrowSessionRejected();
        }
        co_return AuthenticatedDownloadRequest{
            .Url = Endpoint(ServerFor(target), (winrt::hstring{ L"/addon-proxy?url=" }
                + EncodeUriComponent(targetUrl)).c_str()).ToString(),
            .Headers = { { L"Authorization", winrt::hstring{ L"Bearer " } + *token } },
        };
    }
    concurrency::task<VideoHashResult> ApiClient::ComputeVideoHashAsync(
        winrt::hstring streamUrl,
        Security::ProtectedHttpHeaders headers)
    {
        co_return co_await ComputeRemoteVideoHashAsync(
            std::move(streamUrl),
            std::move(headers),
            m_executor);
    }

    concurrency::task<Dto::Capabilities> ApiClient::GetCapabilitiesAsync()
    {
        auto const response = co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(), L"/capabilities");
        co_return Mappers::ParseCapabilities(response);
    }

    concurrency::task<Dto::PeopleSearch> ApiClient::SearchPeopleAsync(winrt::hstring query)
    {
        auto const trimmed = TrimmedText(query);
        if (trimmed.size() < 2 || trimmed.size() > 120)
        {
            throw std::invalid_argument{ "A people search needs 2 to 120 characters." };
        }
        auto const path = winrt::hstring{ L"/people/search?q=" } + EncodeUriComponent(trimmed);
        auto const response = co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(), path.c_str());
        Dto::PeopleSearch answer;
        answer.People = Mappers::ParsePeopleSearch(response);
        answer.Corrected = Mappers::ParsePeopleCorrection(response, answer.People);
        co_return answer;
    }

    concurrency::task<Dto::PersonDetail> ApiClient::GetPersonAsync(std::int64_t tmdbId)
    {
        if (tmdbId <= 0)
        {
            throw std::invalid_argument{ "A person's TMDB id must be positive." };
        }
        auto const path = winrt::hstring{ L"/people/" } + winrt::to_hstring(tmdbId);
        auto const response = co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(), path.c_str());
        co_return Mappers::ParsePerson(response);
    }

    concurrency::task<Dto::IdLookup> ApiClient::LookupAsync(winrt::hstring source, winrt::hstring id)
    {
        if (!IsLookupId(source, id))
        {
            throw std::invalid_argument{ "A lookup needs a TVDB number or an IMDb id." };
        }
        auto const path = winrt::hstring{ L"/lookup?source=" } + source + L"&id=" + id;
        auto const response = co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(), path.c_str());
        co_return Mappers::ParseIdLookup(response);
    }

    concurrency::task<Dto::MagnetRequest> ApiClient::AddMagnetAsync(winrt::hstring infoHash, winrt::hstring name)
    {
        if (infoHash.size() != 40 || !IsLowerHex(infoHash))
        {
            throw std::invalid_argument{ "A magnet is sent as its 40-character hex info hash." };
        }
        // The server takes at most 200 plain characters, display only.
        auto const shownName = TrimmedText(name);
        auto const plainName = shownName.size() <= 200 && std::none_of(shownName.begin(), shownName.end(), [](wchar_t character)
        {
            return std::iswcntrl(character) != 0;
        });
        winrt::Windows::Data::Json::JsonObject body;
        body.Insert(L"infoHash", winrt::Windows::Data::Json::JsonValue::CreateStringValue(infoHash));
        if (!shownName.empty() && plainName)
        {
            body.Insert(L"name", winrt::Windows::Data::Json::JsonValue::CreateStringValue(shownName));
        }
        // The play links are rebuilt on the server this request goes to; a
        // session that moves meanwhile is cancelled by the send itself.
        auto const server = ServerFor(m_tokenProvider->CurrentTarget());
        auto const response = co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Post(), L"/magnets", body.Stringify());
        co_return Mappers::ParseMagnet(response, std::wstring_view{ server });
    }

    concurrency::task<Dto::MagnetRequest> ApiClient::GetMagnetAsync(winrt::hstring id)
    {
        if (!IsMagnetId(id))
        {
            throw std::invalid_argument{ "A magnet request id must be the server's own." };
        }
        auto const server = ServerFor(m_tokenProvider->CurrentTarget());
        auto const path = winrt::hstring{ L"/magnets/" } + id;
        auto const response = co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(), path.c_str());
        co_return Mappers::ParseMagnet(response, std::wstring_view{ server });
    }

    concurrency::task<void> ApiClient::ForgetMagnetAsync(winrt::hstring id)
    {
        if (!IsMagnetId(id))
        {
            throw std::invalid_argument{ "A magnet request id must be the server's own." };
        }
        auto const path = winrt::hstring{ L"/magnets/" } + id;
        static_cast<void>(co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Delete(), path.c_str()));
    }

    concurrency::task<winrt::Windows::Data::Json::IJsonValue> ApiClient::SendAuthenticatedJsonAsync(
        winrt::Windows::Web::Http::HttpMethod method,
        wchar_t const* path,
        std::optional<winrt::hstring> body)
    {
        auto const target = m_tokenProvider->CurrentTarget();
        auto const generation = target.Generation;
        co_await winrt::resume_background();
        RequireSameTarget(target);
        auto token = co_await m_tokenProvider->AccessTokenAsync();
        RequireSameTarget(target);
        if (!token)
        {
            co_await m_tokenProvider->RejectSessionAsync(generation);
            ThrowSessionRejected();
        }

        auto const server = ServerFor(target);
        auto send = [this, &method, &server, path, &body](winrt::hstring const& bearer)
        {
            return m_executor->SendJsonAsync(
                method,
                Endpoint(server, path),
                body,
                { { L"Authorization", winrt::hstring{ L"Bearer " } + bearer } });
        };

        try
        {
            auto response = co_await send(*token);
            RequireSameTarget(target);
            co_return response;
        }
        catch (winrt::hresult_error const& error)
        {
            RequireSameTarget(target);
            auto const status = ApiError::HttpStatus(error.code());
            if (!status || *status != 401)
            {
                throw;
            }
        }

        token = co_await m_tokenProvider->RefreshAccessTokenAsync();
        RequireSameTarget(target);
        if (!token)
        {
            co_await m_tokenProvider->RejectSessionAsync(generation);
            ThrowSessionRejected();
        }

        // The refresh succeeded, so the session is alive: a server can briefly
        // refuse a token the identity provider has just issued (after a
        // signing-key rotation, until its cached key set refreshes). This
        // request fails with its 401 and the session is kept; the next request
        // tries again. Only a refresh that returned nothing above, which the
        // provider reserves for a definitive rejection, ends the session.
        try
        {
            auto response = co_await send(*token);
            RequireSameTarget(target);
            co_return response;
        }
        catch (winrt::hresult_error const&)
        {
            RequireSameTarget(target);
            throw;
        }
    }

    void ApiClient::ThrowSessionRejected()
    {
        throw winrt::hresult_error{
            ApiError::SessionRejected,
            L"The session is no longer valid." };
    }

    winrt::Windows::Foundation::Uri ApiClient::Endpoint(winrt::hstring const& server, wchar_t const* path)
    {
        if (!path || path[0] != L'/')
        {
            throw std::invalid_argument{ "API endpoint paths must begin with a slash." };
        }
        return winrt::Windows::Foundation::Uri{ server + path };
    }
}
