#include "pch.h"
#include "Api/ApiClient.h"
#include "Api/ApiError.h"
#include "Api/HttpExecutor.h"
#include "Services/Auth/ITokenProvider.h"

#include <chrono>
#include <stdexcept>
#include <string>
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

    concurrency::task<Dto::HealthStatus> ApiClient::GetHealthAsync()
    {
        co_await winrt::resume_background();
        auto const started = std::chrono::steady_clock::now();
        auto const value = co_await m_executor->SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            Endpoint(L"/health"));
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
            Endpoint(L"/auth/config"));
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

    concurrency::task<Dto::AddonsPayload> ApiClient::GetAddonsAsync()
    {
        auto const value = co_await SendAuthenticatedJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            L"/addons");
        co_return Mappers::ParseAddons(value);
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
        for(auto const&row:rows){winrt::Windows::Data::Json::JsonObject object;object.Insert(L"videoId",winrt::Windows::Data::Json::JsonValue::CreateStringValue(row.VideoId));object.Insert(L"itemId",winrt::Windows::Data::Json::JsonValue::CreateStringValue(row.ItemId));object.Insert(L"positionSec",winrt::Windows::Data::Json::JsonValue::CreateNumberValue(row.PositionSec));object.Insert(L"durationSec",winrt::Windows::Data::Json::JsonValue::CreateNumberValue(row.DurationSec));object.Insert(L"watched",winrt::Windows::Data::Json::JsonValue::CreateBooleanValue(row.Watched));if(row.Name)object.Insert(L"name",winrt::Windows::Data::Json::JsonValue::CreateStringValue(*row.Name));if(row.Poster)object.Insert(L"poster",winrt::Windows::Data::Json::JsonValue::CreateStringValue(*row.Poster));object.Insert(L"updatedAt",winrt::Windows::Data::Json::JsonValue::CreateNumberValue(static_cast<double>(row.UpdatedAt)));body.Append(object);}
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

    concurrency::task<winrt::Windows::Data::Json::IJsonValue> ApiClient::SendAuthenticatedJsonAsync(
        winrt::Windows::Web::Http::HttpMethod method,
        wchar_t const* path,
        std::optional<winrt::hstring> body)
    {
        co_await winrt::resume_background();

        auto const generation = m_tokenProvider->SessionGeneration();
        auto token = co_await m_tokenProvider->AccessTokenAsync();
        if (!token)
        {
            co_await m_tokenProvider->RejectSessionAsync(generation);
            ThrowSessionRejected();
        }

        auto send = [this, &method, path, &body](winrt::hstring const& bearer)
        {
            return m_executor->SendJsonAsync(
                method,
                Endpoint(path),
                body,
                { { L"Authorization", winrt::hstring{ L"Bearer " } + bearer } });
        };

        try
        {
            co_return co_await send(*token);
        }
        catch (winrt::hresult_error const& error)
        {
            auto const status = ApiError::HttpStatus(error.code());
            if (!status || *status != 401)
            {
                throw;
            }
        }

        token = co_await m_tokenProvider->RefreshAccessTokenAsync();
        if (!token)
        {
            co_await m_tokenProvider->RejectSessionAsync(generation);
            ThrowSessionRejected();
        }

        try
        {
            co_return co_await send(*token);
        }
        catch (winrt::hresult_error const& error)
        {
            auto const status = ApiError::HttpStatus(error.code());
            if (!status || *status != 401)
            {
                throw;
            }
        }

        co_await m_tokenProvider->RejectSessionAsync(generation);
        ThrowSessionRejected();
    }

    void ApiClient::ThrowSessionRejected()
    {
        throw winrt::hresult_error{
            ApiError::SessionRejected,
            L"The session is no longer valid." };
    }

    winrt::Windows::Foundation::Uri ApiClient::Endpoint(wchar_t const* path) const
    {
        if (!path || path[0] != L'/')
        {
            throw std::invalid_argument{ "API endpoint paths must begin with a slash." };
        }
        return winrt::Windows::Foundation::Uri{ m_baseUrl + path };
    }
}
