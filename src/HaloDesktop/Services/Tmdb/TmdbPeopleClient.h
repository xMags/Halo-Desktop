#pragma once

#include "Api/PeopleDirectory.h"
#include "Services/Tmdb/TmdbPeoplePolicy.h"

#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <winrt/Windows.Web.Http.h>

namespace HaloDesktop::Services::Tmdb
{
    class TmdbKeyStore;

    // People search, person pages and IMDb/TVDB lookups straight from TMDB with
    // the viewer's own key (TmdbKeyStore), for Halo without an account. A port
    // of the Halo server's routes for the same three (`/people/search`,
    // `/people/:id`, `/lookup`), answering in the server's shapes through the
    // app's own readers, so a page cannot tell the two apart.
    //
    // Without a usable key every call fails (401). A 401 from TMDB marks the
    // key refused, which turns these features off until it is replaced.
    //
    // Thread-safe: calls may overlap, from any thread. Answers are kept in
    // memory (searches for an hour, people for six, ids for a week) so a busy
    // search box costs TMDB one request per distinct query.
    class TmdbPeopleClient final
        : public ::HaloDesktop::Api::IPeopleDirectory,
          public std::enable_shared_from_this<TmdbPeopleClient>
    {
    public:
        explicit TmdbPeopleClient(std::shared_ptr<TmdbKeyStore> keys);

        TmdbPeopleClient(TmdbPeopleClient const&) = delete;
        TmdbPeopleClient& operator=(TmdbPeopleClient const&) = delete;

        [[nodiscard]] concurrency::task<::HaloDesktop::Api::Dto::PeopleSearch> SearchPeopleAsync(winrt::hstring query) override;
        [[nodiscard]] concurrency::task<::HaloDesktop::Api::Dto::PersonDetail> GetPersonAsync(std::int64_t tmdbId) override;
        [[nodiscard]] concurrency::task<::HaloDesktop::Api::Dto::IdLookup> LookupAsync(winrt::hstring source, winrt::hstring id) override;

    private:
        template <typename T>
        struct Cached final
        {
            std::chrono::steady_clock::time_point Expires;
            T Value;
        };

        template <typename T>
        struct Cache final
        {
            std::map<std::wstring, Cached<T>, std::less<>> Entries;
            std::chrono::steady_clock::duration Lifetime{};
            std::size_t MaximumEntries{};
        };

        // The body of a 200; throws HTTP 404 for a 404 and the status for any
        // other answer, marking the key refused on a 401.
        [[nodiscard]] concurrency::task<winrt::Windows::Data::Json::IJsonValue> GetAsync(
            std::wstring pathAndQuery,
            std::chrono::milliseconds timeout);
        [[nodiscard]] concurrency::task<std::vector<PersonSummary>> SearchNamesAsync(std::wstring query);
        // Empty when TMDB has no IMDb id for the title (that answer is kept too).
        [[nodiscard]] concurrency::task<std::optional<std::wstring>> ImdbIdForAsync(
            TitleType type,
            std::int64_t tmdbId,
            std::chrono::milliseconds timeout);
        struct MappedCredits final
        {
            std::vector<MappedCredit> Credits;
            // Some credit's id is still unknown: the budget ran out or a lookup failed.
            bool Partial{};
        };
        // Each credit's IMDb id, in order; a credit TMDB has none for is left
        // out (the app could neither open nor play it). At most six lookups at
        // once, and none started once eight seconds have passed.
        [[nodiscard]] concurrency::task<MappedCredits> MapCreditsAsync(std::vector<CreditCandidate> candidates);
        [[nodiscard]] concurrency::task<FindResult> FindAsync(std::wstring source, std::wstring id);

        template <typename T>
        [[nodiscard]] std::optional<T> Lookup(Cache<T>& cache, std::wstring const& key);
        template <typename T>
        void Keep(Cache<T>& cache, std::wstring key, T value);

        std::shared_ptr<TmdbKeyStore> m_keys;
        winrt::Windows::Web::Http::HttpClient m_client;
        std::mutex m_cacheMutex;
        Cache<std::vector<PersonSummary>> m_searches;
        Cache<PersonDetails> m_people;
        Cache<std::optional<std::wstring>> m_imdbIds;
        Cache<FindResult> m_finds;
    };
}
