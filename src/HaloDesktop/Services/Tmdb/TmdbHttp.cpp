#include "pch.h"
#include "Services/Tmdb/TmdbHttp.h"

#include "Api/BoundedHttpContent.h"

#include <pplawait.h>
#include <stdexcept>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.Threading.h>
#include <winrt/Windows.Web.Http.Headers.h>

namespace
{
    constexpr wchar_t TmdbOrigin[] = L"https://api.themoviedb.org";
}

namespace HaloDesktop::Services::Tmdb
{
    concurrency::task<TmdbResponse> GetTmdbJsonAsync(
        winrt::Windows::Web::Http::HttpClient client,
        TmdbCredential credential,
        std::wstring pathAndQuery,
        std::chrono::milliseconds timeout)
    {
        namespace Http = winrt::Windows::Web::Http;
        if (!client || !pathAndQuery.starts_with(L"/3/") || credential.Value.empty())
        {
            throw std::invalid_argument{ "A TMDB request needs a client, a /3/ path and a credential." };
        }
        std::wstring url{ TmdbOrigin };
        url += pathAndQuery;
        if (credential.Kind == TmdbCredentialKind::ApiKey)
        {
            url += pathAndQuery.find(L'?') == std::wstring::npos ? L"?api_key=" : L"&api_key=";
            url += credential.Value;
        }
        Http::HttpRequestMessage request{ Http::HttpMethod::Get(), winrt::Windows::Foundation::Uri{ url } };
        request.Headers().Accept().TryParseAdd(L"application/json");
        if (credential.Kind == TmdbCredentialKind::ReadToken)
        {
            request.Headers().Authorization(Http::Headers::HttpCredentialsHeaderValue{ L"Bearer", credential.Value });
        }

        // One clock for the whole request: it cancels the send while headers
        // are awaited and the body read after them.
        concurrency::cancellation_token_source cancellation;
        auto const operation = client.SendRequestAsync(request, Http::HttpCompletionOption::ResponseHeadersRead);
        auto const timer = winrt::Windows::System::Threading::ThreadPoolTimer::CreateTimer(
            [operation, cancellation](winrt::Windows::System::Threading::ThreadPoolTimer const&)
            {
                cancellation.cancel();
                try { operation.Cancel(); } catch (...) {}
            },
            std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(timeout));
        try
        {
            auto const response = co_await operation;
            TmdbResponse result{ .Status = static_cast<std::uint16_t>(response.StatusCode()) };
            if (response.StatusCode() != Http::HttpStatusCode::Ok)
            {
                timer.Cancel();
                co_return result;
            }
            auto const text = co_await ::HaloDesktop::Api::ReadBoundedJsonTextAsync(response.Content(), cancellation.get_token());
            timer.Cancel();
            winrt::Windows::Data::Json::JsonValue body{ nullptr };
            if (!winrt::Windows::Data::Json::JsonValue::TryParse(text, body))
            {
                throw std::runtime_error{ "TMDB answered with something other than JSON." };
            }
            result.Body = body;
            co_return result;
        }
        catch (...)
        {
            timer.Cancel();
            throw;
        }
    }
}
