#pragma once

#include "Api/Dto.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ppltasks.h>
#include <string>
#include <vector>
#include <winrt/Microsoft.UI.Dispatching.h>

namespace HaloDesktop::Api
{
    class ApiClient;
    class BackendSelector;
    class IPeopleDirectory;
}

namespace HaloDesktop::Services
{
    // How a people call ended.
    enum class PeopleOutcome
    {
        Found,
        // Nothing is known by that name or id (404, or no exact name).
        NotFound,
        // The answer could not be had, or came back bad.
        Failed,
    };

    // People search, person pages and id lookups: on the account's server, or
    // without an account straight from TMDB with the viewer's own key
    // (Tmdb::TmdbPeopleClient), which fails every call while there is no
    // usable key (ServerFeaturesService hides every caller then).
    //
    // UI-thread only. Each kind of call is its own channel with its own
    // generation counter: a new call on a channel retires the one in flight
    // there, so the Search page's row and a person page never retire each
    // other. Cancel and an account change retire calls too; a retired call
    // never calls its handler. Handlers run on the UI thread; the coroutines
    // reach the service only weakly, so they may outlive it.
    class PeopleService final : public std::enable_shared_from_this<PeopleService>
    {
    public:
        using SearchHandler = std::function<void(PeopleOutcome, ::HaloDesktop::Api::Dto::PeopleSearch)>;
        // The TMDB id of the person a name stands for (ExactNamePerson).
        using FindHandler = std::function<void(PeopleOutcome, std::int64_t)>;
        using PersonHandler = std::function<void(PeopleOutcome, std::optional<::HaloDesktop::Api::Dto::PersonDetail>)>;
        using LookupHandler = std::function<void(PeopleOutcome, std::optional<::HaloDesktop::Api::Dto::IdLookup>)>;

        PeopleService(
            std::shared_ptr<::HaloDesktop::Api::ApiClient> api,
            std::shared_ptr<::HaloDesktop::Api::IPeopleDirectory> withoutAccount,
            std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher);

        PeopleService(PeopleService const&) = delete;
        PeopleService& operator=(PeopleService const&) = delete;

        // Search's people row: who matches the query.
        void SearchPeople(std::wstring query, SearchHandler handler);
        void CancelSearch() noexcept;
        // A person page opened by name: searches it and keeps the exact match.
        void FindPerson(std::wstring name, FindHandler handler);
        void LoadPerson(std::int64_t tmdbId, PersonHandler handler);
        void CancelPerson() noexcept;
        // Search's results for a person's exact name: the same person answer,
        // on its own channel so an open person page is never retired by it.
        void LoadFilmography(std::int64_t tmdbId, PersonHandler handler);
        void CancelFilmography() noexcept;
        // Search's card for a pasted TVDB number or IMDb id.
        void Lookup(std::wstring source, std::wstring id, LookupHandler handler);
        void CancelLookup() noexcept;
        void OnAccountChanged() noexcept;

    private:
        enum class Channel : std::size_t
        {
            Search,
            Person,
            Lookup,
            Filmography,
        };
        static constexpr std::size_t ChannelCount = 4;

        // Who answers now: the account's server, or TMDB without an account.
        [[nodiscard]] std::shared_ptr<::HaloDesktop::Api::IPeopleDirectory> Directory() const;
        [[nodiscard]] std::uint64_t Begin(Channel channel) noexcept;
        [[nodiscard]] bool IsCurrent(Channel channel, std::uint64_t generation) const noexcept;
        // Runs `start` (off the UI thread) and hands its outcome to `deliver`
        // on the UI thread, unless the call was retired by then.
        template <typename T>
        static concurrency::task<void> RunAsync(
            std::weak_ptr<PeopleService> weak,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
            Channel channel,
            std::uint64_t generation,
            std::function<concurrency::task<T>()> start,
            std::function<void(PeopleOutcome, std::optional<T>)> deliver);
        template <typename T>
        void Start(
            Channel channel,
            std::function<concurrency::task<T>()> start,
            std::function<void(PeopleOutcome, std::optional<T>)> deliver);

        std::shared_ptr<::HaloDesktop::Api::ApiClient> m_api;
        std::shared_ptr<::HaloDesktop::Api::IPeopleDirectory> m_withoutAccount;
        std::shared_ptr<::HaloDesktop::Api::BackendSelector> m_backends;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        std::array<std::uint64_t, ChannelCount> m_generations{};
    };
}
