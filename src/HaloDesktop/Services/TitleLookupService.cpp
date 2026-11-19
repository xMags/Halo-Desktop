#include "pch.h"
#include "Services/TitleLookupService.h"

#include "Api/BackendSelector.h"
#include "Api/Dto.h"
#include "Api/HaloBackend.h"
#include "Services/SearchService.h"

#include <pplawait.h>
#include <stdexcept>
#include <utility>
#include <wil/cppwinrt_helpers.h>

namespace
{
    using MetaAnswer = std::optional<::HaloDesktop::Api::Dto::MetaDetail>;

    // One typed meta call; a failure (thrown before or after the call is
    // under way) is no answer, so the other call is still awaited.
    concurrency::task<MetaAnswer> AskAsync(
        std::shared_ptr<::HaloDesktop::Api::IHaloBackend> backend,
        winrt::hstring type,
        winrt::hstring id)
    {
        try
        {
            co_return co_await backend->GetMetaAsync(std::move(type), std::move(id));
        }
        catch (...)
        {
            co_return std::nullopt;
        }
    }

    // An answer counts only when it is of the type asked and names something.
    bool Counts(MetaAnswer const& answer, wchar_t const* type)
    {
        return answer && answer->Preview.Type == type && !answer->Preview.Id.empty() && !answer->Preview.Name.empty();
    }

    std::optional<::HaloDesktop::Services::SearchTitle> Choose(MetaAnswer const& movie, MetaAnswer const& series)
    {
        auto const movieCounts = Counts(movie, L"movie");
        auto const seriesCounts = Counts(series, L"series");
        if (seriesCounts && !series->Videos.empty()) return ::HaloDesktop::Services::ToSearchTitle(series->Preview);
        if (movieCounts) return ::HaloDesktop::Services::ToSearchTitle(movie->Preview);
        if (seriesCounts) return ::HaloDesktop::Services::ToSearchTitle(series->Preview);
        return std::nullopt;
    }
}

namespace HaloDesktop::Services
{
    TitleLookupService::TitleLookupService(
        std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher)
        : m_backends(std::move(backends)), m_dispatcher(std::move(dispatcher))
    {
        if (!m_backends || !m_dispatcher)
        {
            throw std::invalid_argument{ "TitleLookupService requires a backend selector and a dispatcher." };
        }
    }

    void TitleLookupService::Lookup(std::wstring imdbId, Handler handler)
    {
        auto const generation = ++m_generation;
        if (imdbId.empty() || !handler)
        {
            return;
        }
        RunAsync(weak_from_this(), m_backends, m_dispatcher, generation, std::move(imdbId), std::move(handler))
            .then([](concurrency::task<void> finished)
            {
                try { finished.get(); } catch (...) {}
            });
    }

    void TitleLookupService::Cancel() noexcept
    {
        ++m_generation;
    }

    void TitleLookupService::OnAccountChanged() noexcept
    {
        ++m_generation;
    }

    concurrency::task<void> TitleLookupService::RunAsync(
        std::weak_ptr<TitleLookupService> weak,
        std::shared_ptr<::HaloDesktop::Api::BackendSelector> backends,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
        std::uint64_t generation,
        std::wstring imdbId,
        Handler handler)
    {
        MetaAnswer movie;
        MetaAnswer series;
        if (auto const backend = backends->Current())
        {
            winrt::hstring const id{ imdbId };
            // Both are under way before either is awaited.
            auto movieTask = AskAsync(backend, L"movie", id);
            auto seriesTask = AskAsync(backend, L"series", id);
            movie = co_await movieTask;
            series = co_await seriesTask;
        }
        auto chosen = Choose(movie, series);

        co_await wil::resume_foreground(dispatcher);
        auto const self = weak.lock();
        if (!self || generation != self->m_generation)
        {
            co_return;
        }
        handler(std::move(chosen));
    }
}
