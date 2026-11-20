#include "pch.h"
#include "Services/Tmdb/TmdbPeopleClient.h"

#include "Api/ApiError.h"
#include "Services/SearchResultsPolicy.h"
#include "Services/Tmdb/TmdbHttp.h"
#include "Services/Tmdb/TmdbKeyStore.h"

#include <algorithm>
#include <cwctype>
#include <pplawait.h>
#include <stdexcept>
#include <utility>
#include <winrt/Windows.Foundation.h>

namespace
{
    namespace Json = winrt::Windows::Data::Json;
    using namespace std::chrono_literals;

    constexpr std::size_t CreditMappingConcurrency = 6;
    constexpr std::chrono::milliseconds CreditMappingBudget{ 8000 };

    [[noreturn]] void FailWith(std::uint16_t status)
    {
        throw winrt::hresult_error{ ::HaloDesktop::Api::ApiError::MakeHttpStatus(status) };
    }

    bool AllDigits(std::wstring_view text) noexcept
    {
        return !text.empty() && std::all_of(text.begin(), text.end(), [](wchar_t character)
        {
            return character >= L'0' && character <= L'9';
        });
    }

    // The same ids the server accepts: a TVDB number, or `tt`/`nm` and digits.
    bool IsLookupId(std::wstring_view source, std::wstring_view id) noexcept
    {
        if (source == L"tvdb") return id.size() <= 10 && AllDigits(id);
        if (source != L"imdb" || id.size() < 3 || id.size() > 12) return false;
        return (id.starts_with(L"tt") || id.starts_with(L"nm")) && AllDigits(id.substr(2));
    }

    std::wstring Lower(std::wstring text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        return text;
    }

    std::wstring Escaped(std::wstring const& text)
    {
        return std::wstring{ winrt::Windows::Foundation::Uri::EscapeComponent(text) };
    }
}

namespace HaloDesktop::Services::Tmdb
{
    TmdbPeopleClient::TmdbPeopleClient(std::shared_ptr<TmdbKeyStore> keys)
        : m_keys(std::move(keys))
    {
        if (!m_keys)
        {
            throw std::invalid_argument{ "TmdbPeopleClient requires the key store." };
        }
        // The server's lifetimes; fewer entries, for one viewer.
        m_searches.Lifetime = 1h;
        m_searches.MaximumEntries = 200;
        m_people.Lifetime = 6h;
        m_people.MaximumEntries = 100;
        m_imdbIds.Lifetime = std::chrono::hours{ 24 * 7 };
        m_imdbIds.MaximumEntries = 5000;
        m_finds.Lifetime = std::chrono::hours{ 24 * 7 };
        m_finds.MaximumEntries = 500;
    }

    template <typename T>
    std::optional<T> TmdbPeopleClient::Lookup(Cache<T>& cache, std::wstring const& key)
    {
        std::scoped_lock const lock{ m_cacheMutex };
        auto const found = cache.Entries.find(key);
        if (found == cache.Entries.end()) return std::nullopt;
        if (found->second.Expires <= std::chrono::steady_clock::now())
        {
            cache.Entries.erase(found);
            return std::nullopt;
        }
        return found->second.Value;
    }

    template <typename T>
    void TmdbPeopleClient::Keep(Cache<T>& cache, std::wstring key, T value)
    {
        std::scoped_lock const lock{ m_cacheMutex };
        auto const now = std::chrono::steady_clock::now();
        if (cache.Entries.size() >= cache.MaximumEntries)
        {
            // Expired entries first; a cache still full of live ones starts over.
            std::erase_if(cache.Entries, [now](auto const& entry) { return entry.second.Expires <= now; });
            if (cache.Entries.size() >= cache.MaximumEntries) cache.Entries.clear();
        }
        cache.Entries.insert_or_assign(std::move(key), Cached<T>{ .Expires = now + cache.Lifetime, .Value = std::move(value) });
    }

    concurrency::task<Json::IJsonValue> TmdbPeopleClient::GetAsync(std::wstring pathAndQuery, std::chrono::milliseconds timeout)
    {
        auto const self = shared_from_this();
        auto const credential = m_keys->Current();
        if (!credential)
        {
            FailWith(401);
        }
        auto const response = co_await GetTmdbJsonAsync(m_client, *credential, std::move(pathAndQuery), timeout);
        if (response.Status == 200 && response.Body)
        {
            co_return response.Body;
        }
        if (response.Status == 401)
        {
            m_keys->MarkRefused(*credential);
        }
        FailWith(response.Status == 0 ? 502 : response.Status);
    }

    concurrency::task<std::vector<PersonSummary>> TmdbPeopleClient::SearchNamesAsync(std::wstring query)
    {
        auto const self = shared_from_this();
        auto key = Lower(query);
        if (auto cached = Lookup(m_searches, key)) co_return std::move(*cached);
        auto const body = co_await GetAsync(L"/3/search/person?query=" + Escaped(query) + L"&include_adult=false", TmdbRequestTimeout);
        auto people = ParsePeopleSearch(body);
        Keep(m_searches, std::move(key), people);
        co_return people;
    }

    concurrency::task<::HaloDesktop::Api::Dto::PeopleSearch> TmdbPeopleClient::SearchPeopleAsync(winrt::hstring query)
    {
        auto const self = shared_from_this();
        auto const trimmed = ::HaloDesktop::Services::TrimmedText(query);
        if (trimmed.size() < 2 || trimmed.size() > 120)
        {
            throw std::invalid_argument{ "A people search needs 2 to 120 characters." };
        }
        auto people = co_await SearchNamesAsync(trimmed);
        std::optional<std::wstring> corrected;
        if (people.empty())
        {
            // Nobody by that spelling: the people it most likely means. A
            // failure here is still "nobody", as on the server.
            auto const words = ClosestPeopleWords(trimmed);
            if (!words.empty())
            {
                std::vector<concurrency::task<std::optional<std::vector<PersonSummary>>>> searches;
                for (auto const& word : words)
                {
                    searches.push_back(SearchNamesAsync(word).then(
                        [](concurrency::task<std::vector<PersonSummary>> found) -> std::optional<std::vector<PersonSummary>>
                        {
                            try { return found.get(); } catch (...) { return std::nullopt; }
                        }));
                }
                auto const results = co_await concurrency::when_all(searches.begin(), searches.end());
                std::vector<std::vector<PersonSummary>> answers;
                for (auto const& result : results)
                {
                    if (result) answers.push_back(*result);
                }
                if (auto closest = RankClosestPeople(trimmed, answers))
                {
                    people = std::move(closest->People);
                    corrected = std::move(closest->Corrected);
                }
            }
        }
        auto const answer = PeopleSearchAnswer(people, corrected);
        ::HaloDesktop::Api::Dto::PeopleSearch result;
        result.People = ::HaloDesktop::Api::Mappers::ParsePeopleSearch(answer);
        result.Corrected = ::HaloDesktop::Api::Mappers::ParsePeopleCorrection(answer, result.People);
        co_return result;
    }

    concurrency::task<std::optional<std::wstring>> TmdbPeopleClient::ImdbIdForAsync(
        TitleType type,
        std::int64_t tmdbId,
        std::chrono::milliseconds timeout)
    {
        auto const self = shared_from_this();
        auto const kind = type == TitleType::Movie ? std::wstring{ L"movie" } : std::wstring{ L"tv" };
        auto key = kind + L":" + std::to_wstring(tmdbId);
        if (auto cached = Lookup(m_imdbIds, key)) co_return std::move(*cached);
        std::optional<std::wstring> imdbId;
        try
        {
            auto const body = co_await GetAsync(L"/3/" + kind + L"/" + std::to_wstring(tmdbId) + L"/external_ids", timeout);
            imdbId = ParseImdbId(body);
        }
        catch (winrt::hresult_error const& error)
        {
            // TMDB no longer knowing the title is an answer ("none"), not a failure.
            if (error.code() != ::HaloDesktop::Api::ApiError::MakeHttpStatus(404)) throw;
        }
        Keep(m_imdbIds, std::move(key), imdbId);
        co_return imdbId;
    }

    concurrency::task<TmdbPeopleClient::MappedCredits> TmdbPeopleClient::MapCreditsAsync(std::vector<CreditCandidate> candidates)
    {
        auto const self = shared_from_this();
        struct Outcome final
        {
            bool Answered{};
            std::optional<std::wstring> ImdbId;
        };
        std::vector<std::optional<std::wstring>> ids(candidates.size());
        MappedCredits mapped;
        auto const deadline = std::chrono::steady_clock::now() + CreditMappingBudget;
        std::size_t next = 0;
        while (next < candidates.size())
        {
            auto const now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                mapped.Partial = true;
                break;
            }
            // No lookup may outlast the budget.
            auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            auto const timeout = (std::min)(remaining, TmdbRequestTimeout);
            auto const end = (std::min)(next + CreditMappingConcurrency, candidates.size());
            std::vector<concurrency::task<Outcome>> lookups;
            for (auto index = next; index < end; ++index)
            {
                auto const& title = candidates[index].Title;
                lookups.push_back(ImdbIdForAsync(title.Type, title.TmdbId, timeout).then(
                    [](concurrency::task<std::optional<std::wstring>> found) -> Outcome
                    {
                        try { return Outcome{ .Answered = true, .ImdbId = found.get() }; } catch (...) { return Outcome{}; }
                    }));
            }
            auto const outcomes = co_await concurrency::when_all(lookups.begin(), lookups.end());
            for (std::size_t offset = 0; offset < outcomes.size(); ++offset)
            {
                if (!outcomes[offset].Answered)
                {
                    mapped.Partial = true;
                    continue;
                }
                ids[next + offset] = outcomes[offset].ImdbId;
            }
            next = end;
        }
        for (std::size_t index = 0; index < candidates.size(); ++index)
        {
            if (ids[index]) mapped.Credits.push_back(MappedCredit{ .Credit = std::move(candidates[index]), .ImdbId = std::move(*ids[index]) });
        }
        co_return mapped;
    }

    concurrency::task<::HaloDesktop::Api::Dto::PersonDetail> TmdbPeopleClient::GetPersonAsync(std::int64_t tmdbId)
    {
        auto const self = shared_from_this();
        if (tmdbId <= 0)
        {
            throw std::invalid_argument{ "A person's TMDB id must be positive." };
        }
        auto key = std::to_wstring(tmdbId);
        auto details = Lookup(m_people, key);
        if (!details)
        {
            // A 404 propagates as one: no such person.
            auto const body = co_await GetAsync(
                L"/3/person/" + key + L"?append_to_response=combined_credits,external_ids",
                TmdbRequestTimeout);
            details = ParsePersonDetails(body);
            // An answer without a usable name is no such person.
            if (!details) FailWith(404);
            Keep(m_people, std::move(key), *details);
        }
        auto const mapped = co_await MapCreditsAsync(details->Credits);
        auto const answer = PersonAnswer(*details, mapped.Credits, mapped.Partial);
        co_return ::HaloDesktop::Api::Mappers::ParsePerson(answer);
    }

    concurrency::task<FindResult> TmdbPeopleClient::FindAsync(std::wstring source, std::wstring id)
    {
        auto const self = shared_from_this();
        auto key = source + L":" + id;
        if (auto cached = Lookup(m_finds, key)) co_return std::move(*cached);
        auto const externalSource = source == L"tvdb" ? L"tvdb_id" : L"imdb_id";
        FindResult found;
        try
        {
            auto const body = co_await GetAsync(L"/3/find/" + Escaped(id) + L"?external_source=" + externalSource, TmdbRequestTimeout);
            found = ParseFind(body);
        }
        catch (winrt::hresult_error const& error)
        {
            if (error.code() != ::HaloDesktop::Api::ApiError::MakeHttpStatus(404)) throw;
        }
        Keep(m_finds, std::move(key), found);
        co_return found;
    }

    concurrency::task<::HaloDesktop::Api::Dto::IdLookup> TmdbPeopleClient::LookupAsync(winrt::hstring source, winrt::hstring id)
    {
        auto const self = shared_from_this();
        std::wstring const from{ source };
        std::wstring const value{ id };
        if (!IsLookupId(from, value))
        {
            throw std::invalid_argument{ "A lookup needs a TVDB number or an IMDb id." };
        }
        auto const found = co_await FindAsync(from, value);
        if (from == L"imdb" && value.starts_with(L"nm"))
        {
            if (found.People.empty()) FailWith(404);
            co_return ::HaloDesktop::Api::Mappers::ParseIdLookup(PersonLookupAnswer(found.People.front()));
        }
        // TVDB numbers series first; an IMDb title id names one or the other.
        auto const& first = from == L"tvdb" ? found.Series : found.Movies;
        auto const& second = from == L"tvdb" ? found.Movies : found.Series;
        if (first.empty() && second.empty()) FailWith(404);
        auto const title = first.empty() ? second.front() : first.front();
        std::optional<std::wstring> imdbId;
        if (from == L"imdb")
        {
            imdbId = value;
        }
        else
        {
            imdbId = co_await ImdbIdForAsync(title.Type, title.TmdbId, TmdbRequestTimeout);
        }
        // Without an IMDb id the app could neither open nor play it.
        if (!imdbId) FailWith(404);
        co_return ::HaloDesktop::Api::Mappers::ParseIdLookup(TitleLookupAnswer(title, *imdbId));
    }
}
