#include "pch.h"
#include "Api/HttpExecutor.h"
#include "Api/ApiError.h"
#include "Api/BoundedHttpContent.h"

#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.Headers.h>

namespace
{
    winrt::Windows::Web::Http::Filters::HttpBaseProtocolFilter CreateFilter(bool allowRedirect)
    {
        winrt::Windows::Web::Http::Filters::HttpBaseProtocolFilter filter;
        filter.CacheControl().ReadBehavior(
            winrt::Windows::Web::Http::Filters::HttpCacheReadBehavior::NoCache);
        filter.CacheControl().WriteBehavior(
            winrt::Windows::Web::Http::Filters::HttpCacheWriteBehavior::NoCache);
        filter.AllowAutoRedirect(allowRedirect);
        return filter;
    }

    bool IsCancellation(winrt::hresult code) noexcept
    {
        return code.value == E_ABORT
            || code.value == HRESULT_FROM_WIN32(ERROR_CANCELLED)
            || code.value == HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
    }

    [[noreturn]] void ThrowTransport(winrt::hresult_error const& error)
    {
        if (IsCancellation(error.code()))
        {
            throw;
        }
        throw winrt::hresult_error{
            ::HaloDesktop::Api::ApiError::Transport,
            L"The server could not be reached." };
    }

    winrt::hstring SanitizeMessage(winrt::hstring const& input)
    {
        std::wstring value{ input };
        std::replace_if(value.begin(), value.end(), [](wchar_t character)
        {
            return std::iswcntrl(character) != 0;
        }, L' ');
        while (!value.empty() && std::iswspace(value.front()) != 0)
        {
            value.erase(value.begin());
        }
        while (!value.empty() && std::iswspace(value.back()) != 0)
        {
            value.pop_back();
        }
        if (value.size() > 512)
        {
            value.resize(512);
        }
        return winrt::hstring{ value };
    }

    winrt::hstring ErrorMessage(
        winrt::hstring const& responseBody,
        winrt::hstring const& reasonPhrase)
    {
        try
        {
            auto const object = winrt::Windows::Data::Json::JsonObject::Parse(responseBody);
            if (object.HasKey(L"error")
                && object.GetNamedValue(L"error").ValueType()
                    == winrt::Windows::Data::Json::JsonValueType::String)
            {
                auto const message = SanitizeMessage(object.GetNamedString(L"error"));
                if (!message.empty())
                {
                    return message;
                }
            }
        }
        catch (...)
        {
        }

        auto const reason = SanitizeMessage(reasonPhrase);
        return reason.empty() ? winrt::hstring{ L"The server rejected the request." } : reason;
    }

    void AppendHeaders(
        winrt::Windows::Web::Http::HttpRequestMessage const& request,
        ::HaloDesktop::Api::HttpHeaders const& headers)
    {
        for (auto const& [name, value] : headers)
        {
            auto const nameView = std::wstring_view{ name.c_str(), name.size() };
            auto const valueView = std::wstring_view{ value.c_str(), value.size() };
            if (name.empty()
                || nameView.find(L'\r') != std::wstring_view::npos
                || nameView.find(L'\n') != std::wstring_view::npos
                || valueView.find(L'\r') != std::wstring_view::npos
                || valueView.find(L'\n') != std::wstring_view::npos)
            {
                throw std::invalid_argument{ "An HTTP header is invalid." };
            }
            if (!request.Headers().TryAppendWithoutValidation(name, value))
            {
                throw std::invalid_argument{ "An HTTP header could not be applied." };
            }
        }
    }
}

namespace HaloDesktop::Api
{
    HttpExecutor::HttpExecutor()
        : m_apiFilter(CreateFilter(true)),
          m_noRedirectFilter(CreateFilter(false)),
          m_apiClient(m_apiFilter),
          m_noRedirectClient(m_noRedirectFilter)
    {
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Data::Json::IJsonValue>
        HttpExecutor::SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod method,
            winrt::Windows::Foundation::Uri uri,
            std::optional<winrt::hstring> body,
            HttpHeaders headers)
    {
        co_await winrt::resume_background();

        winrt::Windows::Web::Http::HttpRequestMessage request{ method, uri };
        AppendHeaders(request, headers);
        if (body)
        {
            request.Content(winrt::Windows::Web::Http::HttpStringContent{
                *body,
                winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8,
                L"application/json" });
        }

        winrt::Windows::Web::Http::HttpResponseMessage response{ nullptr };
        try
        {
            response = co_await m_apiClient.SendRequestAsync(
                request,
                winrt::Windows::Web::Http::HttpCompletionOption::ResponseHeadersRead);
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowTransport(error);
        }

        auto const status = static_cast<std::uint16_t>(response.StatusCode());
        if (!response.IsSuccessStatusCode())
        {
            winrt::hstring responseBody;
            try
            {
                responseBody = co_await ReadBoundedJsonTextAsync(response.Content());
            }
            catch (...)
            {
            }
            throw winrt::hresult_error{
                ApiError::MakeHttpStatus(status),
                ErrorMessage(responseBody, response.ReasonPhrase()) };
        }

        winrt::hstring responseBody;
        try
        {
            responseBody = co_await ReadBoundedJsonTextAsync(response.Content());
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowTransport(error);
        }

        try
        {
            co_return winrt::Windows::Data::Json::JsonValue::Parse(responseBody);
        }
        catch (...)
        {
            throw winrt::hresult_invalid_argument{ L"The server returned invalid JSON." };
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Web::Http::HttpResponseMessage>
        HttpExecutor::SendForStreamAsync(winrt::Windows::Web::Http::HttpRequestMessage request)
    {
        co_await winrt::resume_background();
        try
        {
            co_return co_await m_apiClient.SendRequestAsync(
                request,
                winrt::Windows::Web::Http::HttpCompletionOption::ResponseHeadersRead);
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowTransport(error);
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Web::Http::HttpResponseMessage>
        HttpExecutor::SendFormWithoutRedirectAsync(
            winrt::Windows::Foundation::Uri uri,
            winrt::hstring body)
    {
        co_await winrt::resume_background();

        winrt::Windows::Web::Http::HttpRequestMessage request{
            winrt::Windows::Web::Http::HttpMethod::Post(),
            uri };
        request.Headers().Accept().Append(
            winrt::Windows::Web::Http::Headers::HttpMediaTypeWithQualityHeaderValue{
                L"application/json" });
        request.Content(winrt::Windows::Web::Http::HttpStringContent{
            body,
            winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8,
            L"application/x-www-form-urlencoded" });

        try
        {
            co_return co_await m_noRedirectClient.SendRequestAsync(
                request,
                winrt::Windows::Web::Http::HttpCompletionOption::ResponseHeadersRead);
        }
        catch (winrt::hresult_error const& error)
        {
            ThrowTransport(error);
        }
    }
}
