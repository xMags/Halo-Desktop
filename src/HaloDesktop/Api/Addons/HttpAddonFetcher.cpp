#include "pch.h"
#include "Api/Addons/HttpAddonFetcher.h"

#include "Api/Addons/AddonClientPolicy.h"
#include "Api/BoundedHttpContent.h"

#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <wil/resource.h>
#include <winrt/Windows.System.Threading.h>
#include <winrt/Windows.Web.Http.Headers.h>

namespace
{
    namespace Addons = HaloDesktop::Api::Addons;
    namespace Http = winrt::Windows::Web::Http;

    enum class StopReason
    {
        None,
        Timeout,
        Cancelled,
    };

    // Whatever part of one request is in flight, so that the budget timer or
    // the caller's cancellation can stop it from another thread. The lock is
    // never held while cancelling or closing.
    class InFlight final
    {
    public:
        void Track(winrt::Windows::Foundation::IAsyncInfo const& operation)
        {
            bool stopped{};
            {
                std::scoped_lock const lock{ m_mutex };
                if (m_finished) return;
                m_operation = operation;
                stopped = m_reason != StopReason::None;
            }
            if (stopped) Cancel(operation);
        }

        void Track(Http::HttpResponseMessage const& response)
        {
            bool stopped{};
            {
                std::scoped_lock const lock{ m_mutex };
                if (m_finished) return;
                m_response = response;
                stopped = m_reason != StopReason::None;
            }
            if (stopped) Close(response);
        }

        // The first reason wins; a finished request is left alone.
        void Stop(StopReason reason) noexcept
        {
            winrt::Windows::Foundation::IAsyncInfo operation{ nullptr };
            Http::HttpResponseMessage response{ nullptr };
            {
                std::scoped_lock const lock{ m_mutex };
                if (m_finished || m_reason != StopReason::None) return;
                m_reason = reason;
                operation = m_operation;
                response = m_response;
            }
            // The body read is stopped through the token: closing the
            // response does not stop a read already in flight.
            try { m_reads.cancel(); } catch (...) {}
            Cancel(operation);
            Close(response);
        }

        // Cancelled when the request is stopped.
        [[nodiscard]] concurrency::cancellation_token ReadCancellation() const
        {
            return m_reads.get_token();
        }

        void Finish() noexcept
        {
            std::scoped_lock const lock{ m_mutex };
            m_finished = true;
            m_operation = nullptr;
            m_response = nullptr;
        }

        [[nodiscard]] StopReason Reason() const
        {
            std::scoped_lock const lock{ m_mutex };
            return m_reason;
        }

    private:
        static void Cancel(winrt::Windows::Foundation::IAsyncInfo const& operation) noexcept
        {
            if (!operation) return;
            try { operation.Cancel(); } catch (...) {}
        }

        static void Close(Http::HttpResponseMessage const& response) noexcept
        {
            if (!response) return;
            try { response.Close(); } catch (...) {}
        }

        mutable std::mutex m_mutex;
        StopReason m_reason{ StopReason::None };
        bool m_finished{};
        winrt::Windows::Foundation::IAsyncInfo m_operation{ nullptr };
        Http::HttpResponseMessage m_response{ nullptr };
        // Thread-safe by itself; outside the lock.
        concurrency::cancellation_token_source m_reads;
    };

    [[noreturn]] void Fail(Addons::AddonFailureKind kind)
    {
        throw Addons::AddonRequestFailure{ { kind, std::nullopt } };
    }

    // A stopped request fails for the reason it was stopped, whatever error
    // the stop itself caused.
    void ThrowIfStopped(InFlight const& inFlight)
    {
        switch (inFlight.Reason())
        {
        case StopReason::Timeout: Fail(Addons::AddonFailureKind::Timeout);
        case StopReason::Cancelled: throw winrt::hresult_canceled{};
        case StopReason::None: break;
        }
    }

    bool IsRedirect(std::uint16_t status) noexcept
    {
        return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    }

    std::optional<winrt::hstring> LocationOf(Http::HttpResponseMessage const& response)
    {
        auto const headers = response.Headers();
        if (!headers.HasKey(L"Location")) return std::nullopt;
        auto const value = headers.Lookup(L"Location");
        if (value.empty()) return std::nullopt;
        return value;
    }

    void CloseQuietly(Http::HttpResponseMessage const& response) noexcept
    {
        try { response.Close(); } catch (...) {}
    }

    Http::Filters::HttpBaseProtocolFilter CreateAddonFilter()
    {
        Http::Filters::HttpBaseProtocolFilter filter;
        filter.CacheControl().ReadBehavior(Http::Filters::HttpCacheReadBehavior::NoCache);
        filter.CacheControl().WriteBehavior(Http::Filters::HttpCacheWriteBehavior::NoCache);
        filter.AllowAutoRedirect(false);
        filter.AllowUI(false);
        filter.CookieUsageBehavior(Http::Filters::HttpCookieUsageBehavior::NoCookies);
        return filter;
    }
}

namespace HaloDesktop::Api::Addons
{
    HttpAddonFetcher::HttpAddonFetcher()
        : m_filter(CreateAddonFilter()),
          m_client(m_filter)
    {
    }

    concurrency::task<AddonFetchResponse> HttpAddonFetcher::GetAsync(
        std::wstring url,
        std::chrono::milliseconds budget,
        concurrency::cancellation_token cancellation)
    {
        // Copied before the first suspension, so nothing below reaches back
        // into this object.
        auto const client = m_client;
        co_await winrt::resume_background();
        if (cancellation.is_canceled()) throw winrt::hresult_canceled{};

        auto const inFlight = std::make_shared<InFlight>();
        auto const timer = winrt::Windows::System::Threading::ThreadPoolTimer::CreateTimer(
            [inFlight](winrt::Windows::System::Threading::ThreadPoolTimer const&)
            {
                inFlight->Stop(StopReason::Timeout);
            },
            std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(budget));
        std::optional<concurrency::cancellation_token_registration> registration;
        if (cancellation.is_cancelable())
        {
            registration = cancellation.register_callback([inFlight]()
            {
                inFlight->Stop(StopReason::Cancelled);
            });
        }
        auto cleanup = wil::scope_exit([&timer, &cancellation, &registration, &inFlight]() noexcept
        {
            try { timer.Cancel(); } catch (...) {}
            if (registration)
            {
                try { cancellation.deregister_callback(*registration); } catch (...) {}
            }
            inFlight->Finish();
        });

        auto current = std::move(url);
        for (std::size_t redirects = 0;; ++redirects)
        {
            ThrowIfStopped(*inFlight);
            if (!IsHttpUrl(current)) Fail(AddonFailureKind::BlockedTarget);
            winrt::Windows::Foundation::Uri uri{ nullptr };
            try
            {
                uri = winrt::Windows::Foundation::Uri{ current };
            }
            catch (...)
            {
                Fail(AddonFailureKind::BlockedTarget);
            }
            if (uri.SchemeName() != L"http" && uri.SchemeName() != L"https") Fail(AddonFailureKind::BlockedTarget);

            Http::HttpRequestMessage request{ Http::HttpMethod::Get(), uri };
            Http::HttpResponseMessage response{ nullptr };
            try
            {
                auto const operation = client.SendRequestAsync(request, Http::HttpCompletionOption::ResponseHeadersRead);
                inFlight->Track(operation);
                response = co_await operation;
            }
            catch (winrt::hresult_error const&)
            {
                ThrowIfStopped(*inFlight);
                Fail(AddonFailureKind::Unavailable);
            }
            inFlight->Track(response);
            if (inFlight->Reason() != StopReason::None)
            {
                CloseQuietly(response);
                ThrowIfStopped(*inFlight);
            }

            auto const status = static_cast<std::uint16_t>(response.StatusCode());
            if (IsRedirect(status))
            {
                auto const location = LocationOf(response);
                if (!location)
                {
                    CloseQuietly(response);
                    co_return AddonFetchResponse{ status, {} };
                }
                CloseQuietly(response);
                if (redirects >= MaximumRedirects) Fail(AddonFailureKind::Unavailable);
                try
                {
                    current = std::wstring{ winrt::Windows::Foundation::Uri{ uri.AbsoluteUri(), *location }.AbsoluteUri() };
                }
                catch (...)
                {
                    Fail(AddonFailureKind::Unavailable);
                }
                continue;
            }
            if (status < 200 || status > 299)
            {
                CloseQuietly(response);
                co_return AddonFetchResponse{ status, {} };
            }

            winrt::hstring body;
            try
            {
                body = co_await ::HaloDesktop::Api::ReadBoundedJsonTextAsync(response.Content(), inFlight->ReadCancellation());
            }
            catch (std::length_error const&)
            {
                CloseQuietly(response);
                ThrowIfStopped(*inFlight);
                Fail(AddonFailureKind::InvalidResponse);
            }
            catch (std::invalid_argument const&)
            {
                CloseQuietly(response);
                ThrowIfStopped(*inFlight);
                Fail(AddonFailureKind::InvalidResponse);
            }
            catch (winrt::hresult_error const&)
            {
                CloseQuietly(response);
                ThrowIfStopped(*inFlight);
                Fail(AddonFailureKind::Unavailable);
            }
            CloseQuietly(response);
            co_return AddonFetchResponse{ status, body };
        }
    }
}
