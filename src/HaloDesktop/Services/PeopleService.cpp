#include "pch.h"
#include "Services/PeopleService.h"

#include "Api/ApiClient.h"
#include "Api/ApiError.h"
#include "Api/BackendSelector.h"
#include "Services/PeopleRowPolicy.h"
#include "Services/SearchResultsPolicy.h"

#include <pplawait.h>
#include <stdexcept>
#include <utility>
#include <wil/cppwinrt_helpers.h>

namespace HaloDesktop::Services
{
    namespace
    {
        // The server's own bounds on a people search.
        bool IsPeopleQuery(std::wstring const& query)
        {
            auto const trimmed = TrimmedText(query);
            return trimmed.size() >= 2 && trimmed.size() <= 120;
        }
    }

    PeopleService::PeopleService(
        std::shared_ptr<::HaloDesktop::Api::ApiClient> api,
        std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_api(std::move(api)), m_backends(std::move(backends)), m_dispatcher(std::move(dispatcher))
    {
        if (!m_api || !m_backends || !m_dispatcher)
        {
            throw std::invalid_argument{ "PeopleService requires the API client, a backend selector and a dispatcher." };
        }
    }

    std::uint64_t PeopleService::Begin(Channel channel) noexcept
    {
        return ++m_generations[static_cast<std::size_t>(channel)];
    }

    bool PeopleService::IsCurrent(Channel channel, std::uint64_t generation) const noexcept
    {
        return m_generations[static_cast<std::size_t>(channel)] == generation;
    }

    template <typename T>
    void PeopleService::Start(
        Channel channel,
        std::function<concurrency::task<T>()> start,
        std::function<void(PeopleOutcome, std::optional<T>)> deliver)
    {
        auto const generation = Begin(channel);
        // Never without an account: the server these live on is the account's.
        if (m_backends->UsesDevice())
        {
            m_dispatcher.TryEnqueue([weak = weak_from_this(), channel, generation, deliver = std::move(deliver)]()
            {
                auto const self = weak.lock();
                if (self && self->IsCurrent(channel, generation)) deliver(PeopleOutcome::Failed, std::nullopt);
            });
            return;
        }
        RunAsync<T>(weak_from_this(), m_dispatcher, channel, generation, std::move(start), std::move(deliver))
            .then([](concurrency::task<void> finished)
            {
                try { finished.get(); } catch (...) {}
            });
    }

    template <typename T>
    concurrency::task<void> PeopleService::RunAsync(
        std::weak_ptr<PeopleService> weak,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
        Channel channel,
        std::uint64_t generation,
        std::function<concurrency::task<T>()> start,
        std::function<void(PeopleOutcome, std::optional<T>)> deliver)
    {
        std::optional<T> value;
        auto outcome = PeopleOutcome::Failed;
        try
        {
            value = co_await start();
            outcome = PeopleOutcome::Found;
        }
        catch (winrt::hresult_error const& error)
        {
            auto const status = ::HaloDesktop::Api::ApiError::HttpStatus(error.code());
            outcome = status && *status == 404 ? PeopleOutcome::NotFound : PeopleOutcome::Failed;
        }
        catch (...)
        {
            outcome = PeopleOutcome::Failed;
        }

        co_await wil::resume_foreground(dispatcher);
        auto const self = weak.lock();
        if (!self || !self->IsCurrent(channel, generation))
        {
            co_return;
        }
        deliver(outcome, std::move(value));
    }

    void PeopleService::SearchPeople(std::wstring query, SearchHandler handler)
    {
        if (!handler)
        {
            CancelSearch();
            return;
        }
        if (!IsPeopleQuery(query))
        {
            CancelSearch();
            handler(PeopleOutcome::NotFound, {});
            return;
        }
        Start<::HaloDesktop::Api::Dto::PeopleSearch>(
            Channel::Search,
            [api = m_api, query = std::move(query)]() { return api->SearchPeopleAsync(winrt::hstring{ query }); },
            [handler = std::move(handler)](PeopleOutcome outcome, std::optional<::HaloDesktop::Api::Dto::PeopleSearch> people)
            {
                handler(outcome, people ? std::move(*people) : ::HaloDesktop::Api::Dto::PeopleSearch{});
            });
    }

    void PeopleService::CancelSearch() noexcept
    {
        static_cast<void>(Begin(Channel::Search));
    }

    void PeopleService::FindPerson(std::wstring name, FindHandler handler)
    {
        if (!handler)
        {
            CancelPerson();
            return;
        }
        // A name the search would refuse cannot be found by it.
        if (!IsPeopleQuery(name))
        {
            CancelPerson();
            handler(PeopleOutcome::NotFound, 0);
            return;
        }
        Start<::HaloDesktop::Api::Dto::PeopleSearch>(
            Channel::Person,
            [api = m_api, name]() { return api->SearchPeopleAsync(winrt::hstring{ name }); },
            [handler = std::move(handler), name](PeopleOutcome outcome, std::optional<::HaloDesktop::Api::Dto::PeopleSearch> answer)
            {
                if (outcome != PeopleOutcome::Found || !answer)
                {
                    handler(outcome, 0);
                    return;
                }
                // A credit's name is spelled as its addon spells it: only the
                // person of exactly that name, never a nearest guess.
                auto const index = ExactNamePerson(answer->People, name);
                if (!index)
                {
                    handler(PeopleOutcome::NotFound, 0);
                    return;
                }
                handler(PeopleOutcome::Found, answer->People[*index].TmdbId);
            });
    }

    void PeopleService::LoadPerson(std::int64_t tmdbId, PersonHandler handler)
    {
        if (!handler)
        {
            CancelPerson();
            return;
        }
        if (tmdbId <= 0)
        {
            CancelPerson();
            handler(PeopleOutcome::NotFound, std::nullopt);
            return;
        }
        Start<::HaloDesktop::Api::Dto::PersonDetail>(
            Channel::Person,
            [api = m_api, tmdbId]() { return api->GetPersonAsync(tmdbId); },
            std::move(handler));
    }

    void PeopleService::CancelPerson() noexcept
    {
        static_cast<void>(Begin(Channel::Person));
    }

    void PeopleService::LoadFilmography(std::int64_t tmdbId, PersonHandler handler)
    {
        if (!handler)
        {
            CancelFilmography();
            return;
        }
        if (tmdbId <= 0)
        {
            CancelFilmography();
            handler(PeopleOutcome::NotFound, std::nullopt);
            return;
        }
        Start<::HaloDesktop::Api::Dto::PersonDetail>(
            Channel::Filmography,
            [api = m_api, tmdbId]() { return api->GetPersonAsync(tmdbId); },
            std::move(handler));
    }

    void PeopleService::CancelFilmography() noexcept
    {
        static_cast<void>(Begin(Channel::Filmography));
    }

    void PeopleService::Lookup(std::wstring source, std::wstring id, LookupHandler handler)
    {
        if (!handler)
        {
            CancelLookup();
            return;
        }
        Start<::HaloDesktop::Api::Dto::IdLookup>(
            Channel::Lookup,
            [api = m_api, source = std::move(source), id = std::move(id)]()
            {
                return api->LookupAsync(winrt::hstring{ source }, winrt::hstring{ id });
            },
            std::move(handler));
    }

    void PeopleService::CancelLookup() noexcept
    {
        static_cast<void>(Begin(Channel::Lookup));
    }

    void PeopleService::OnAccountChanged() noexcept
    {
        for (auto& generation : m_generations)
        {
            ++generation;
        }
    }

}
