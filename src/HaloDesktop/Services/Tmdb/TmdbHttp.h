#pragma once

#include "Services/Tmdb/TmdbCredential.h"

#include <chrono>
#include <cstdint>
#include <ppltasks.h>
#include <string>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Web.Http.h>

namespace HaloDesktop::Services::Tmdb
{
    // How long one TMDB request may take, the server's own bound.
    inline constexpr std::chrono::milliseconds TmdbRequestTimeout{ 8000 };

    struct TmdbResponse final
    {
        std::uint16_t Status{};
        // The parsed body of a 200; null for any other status.
        winrt::Windows::Data::Json::IJsonValue Body{ nullptr };
    };

    // One GET to `https://api.themoviedb.org` + `pathAndQuery` (which starts
    // with `/3/`) with the viewer's credential: a read token as a Bearer
    // header, an API key as the `api_key` parameter (the only way TMDB takes
    // one). The credential goes to TMDB alone and is never logged or put in an
    // error. The whole request, body included, is given `timeout`; the body is
    // read under the app's JSON size bound.
    //
    // Answers any HTTP status; throws on a timeout, a network failure or a 200
    // whose body is not JSON. Thread-agnostic: the client may be shared.
    [[nodiscard]] concurrency::task<TmdbResponse> GetTmdbJsonAsync(
        winrt::Windows::Web::Http::HttpClient client,
        TmdbCredential credential,
        std::wstring pathAndQuery,
        std::chrono::milliseconds timeout = TmdbRequestTimeout);
}
