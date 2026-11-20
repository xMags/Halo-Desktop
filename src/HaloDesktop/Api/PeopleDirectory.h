#pragma once

#include "Api/Dto.h"

#include <cstdint>
#include <ppltasks.h>
#include <winrt/base.h>

namespace HaloDesktop::Api
{
    // Where people search, person pages and IMDb/TVDB lookups are answered:
    // the account's server (ApiClient), or, without an account, TMDB itself
    // with the viewer's own key (Services::Tmdb::TmdbPeopleClient). Both give
    // the same answers, so nothing above them knows which one it asked.
    //
    // A missing person or title fails with HTTP status 404 (ApiError), which
    // callers read as "not found"; anything else that fails is a failure.
    class IPeopleDirectory
    {
    public:
        virtual ~IPeopleDirectory() = default;

        // People whose name matches `query` (trimmed; 2 to 120 characters).
        [[nodiscard]] virtual concurrency::task<Dto::PeopleSearch> SearchPeopleAsync(winrt::hstring query) = 0;
        [[nodiscard]] virtual concurrency::task<Dto::PersonDetail> GetPersonAsync(std::int64_t tmdbId) = 0;
        // `source` is `tvdb` (a number) or `imdb` (`tt` or `nm` and digits).
        [[nodiscard]] virtual concurrency::task<Dto::IdLookup> LookupAsync(winrt::hstring source, winrt::hstring id) = 0;
    };
}
