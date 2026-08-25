#include "pch.h"
#include "Api/OpenSubtitlesHash.h"

#include "Api/HttpExecutor.h"
#include "Api/OpenSubtitlesHashPolicy.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.Headers.h>

namespace
{
    using HaloDesktop::Api::HashByteRange;
    using HaloDesktop::Api::HashContentRange;

    struct RangeResult final
    {
        std::vector<std::uint8_t> Bytes;
        std::uint64_t Total{};
    };

    concurrency::task<winrt::Windows::Web::Http::HttpResponseMessage> Send(
        std::shared_ptr<HaloDesktop::Api::HttpExecutor> const& executor,
        winrt::hstring const& url,
        winrt::Windows::Web::Http::HttpMethod const& method,
        HaloDesktop::Security::ProtectedHttpHeaders const& protectedHeaders,
        std::optional<HashByteRange> range = std::nullopt)
    {
        winrt::Windows::Web::Http::HttpRequestMessage request{
            method,
            winrt::Windows::Foundation::Uri{ url },
        };
        for (auto const& header : HaloDesktop::Api::BuildHashRequestHeaders(protectedHeaders, range))
        {
            if (!request.Headers().TryAppendWithoutValidation(header.Name, header.Value))
            {
                throw std::invalid_argument{ "A protected source header could not be applied." };
            }
        }
        co_return co_await executor->SendForStreamAsync(request);
    }

    concurrency::task<std::optional<std::uint64_t>> ReadHeadSize(
        winrt::hstring const& url,
        HaloDesktop::Security::ProtectedHttpHeaders const& headers,
        std::shared_ptr<HaloDesktop::Api::HttpExecutor> const& executor)
    {
        auto response = co_await Send(
            executor,
            url,
            winrt::Windows::Web::Http::HttpMethod::Head(),
            headers);
        auto const length = response.Content().Headers().ContentLength();
        if (!response.IsSuccessStatusCode() || !length || length.Value() == 0)
        {
            co_return std::nullopt;
        }
        co_return length.Value();
    }

    std::optional<HashContentRange> ReadContentRange(
        winrt::Windows::Web::Http::HttpResponseMessage const& response)
    {
        auto const value = response.Content().Headers().ContentRange();
        if (!value)
        {
            return std::nullopt;
        }
        auto const first = value.FirstBytePosition();
        auto const last = value.LastBytePosition();
        auto const total = value.Length();
        if (!first || !last || !total)
        {
            return std::nullopt;
        }
        return HashContentRange{ first.Value(), last.Value(), total.Value() };
    }

    concurrency::task<std::vector<std::uint8_t>> ReadExactBody(
        winrt::Windows::Web::Http::HttpResponseMessage const& response,
        std::size_t expectedLength)
    {
        auto const contentLength = response.Content().Headers().ContentLength();
        if (contentLength && contentLength.Value() != expectedLength)
        {
            throw std::runtime_error{ "The source returned an incomplete hash range." };
        }

        auto input = co_await response.Content().ReadAsInputStreamAsync();
        winrt::Windows::Storage::Streams::DataReader reader{ input };
        reader.InputStreamOptions(winrt::Windows::Storage::Streams::InputStreamOptions::Partial);
        std::vector<std::uint8_t> bytes;
        bytes.reserve(expectedLength);
        for (;;)
        {
            auto const remaining = expectedLength + 1 - bytes.size();
            auto const requestSize = static_cast<std::uint32_t>((std::min)(remaining, std::size_t{ 16 * 1024 }));
            auto const loaded = co_await reader.LoadAsync(requestSize);
            if (loaded == 0)
            {
                break;
            }
            auto const offset = bytes.size();
            bytes.resize(offset + loaded);
            reader.ReadBytes(winrt::array_view<std::uint8_t>{
                bytes.data() + offset,
                bytes.data() + offset + loaded,
            });
            if (bytes.size() > expectedLength)
            {
                throw std::runtime_error{ "The source returned an oversized hash range." };
            }
        }
        co_return bytes;
    }

    concurrency::task<RangeResult> ReadRange(
        winrt::hstring const& url,
        HaloDesktop::Security::ProtectedHttpHeaders const& headers,
        std::shared_ptr<HaloDesktop::Api::HttpExecutor> const& executor,
        HashByteRange requestedRange,
        std::optional<std::uint64_t> expectedTotal)
    {
        auto response = co_await Send(
            executor,
            url,
            winrt::Windows::Web::Http::HttpMethod::Get(),
            headers,
            requestedRange);
        auto const expectedLength = requestedRange.End - requestedRange.Start + 1;
        if (expectedLength > (std::numeric_limits<std::size_t>::max)())
        {
            throw std::runtime_error{ "The requested hash range is too large." };
        }
        auto bytes = co_await ReadExactBody(response, static_cast<std::size_t>(expectedLength));
        auto const total = HaloDesktop::Api::ValidateHashRangeResponse(
            static_cast<std::uint16_t>(response.StatusCode()),
            ReadContentRange(response),
            requestedRange,
            expectedTotal,
            bytes.size());
        co_return RangeResult{ std::move(bytes), total };
    }
}

namespace HaloDesktop::Api
{
    concurrency::task<VideoHashResult> ComputeRemoteVideoHashAsync(
        winrt::hstring url,
        Security::ProtectedHttpHeaders headers,
        std::shared_ptr<HttpExecutor> executor)
    {
        Security::ValidateProtectedHttpHeaders(headers);
        auto const headSize = co_await ReadHeadSize(url, headers, executor);
        auto const head = co_await ReadRange(
            url,
            headers,
            executor,
            { 0, OpenSubtitlesHashChunkSize - 1 },
            headSize);
        auto const size = headSize.value_or(head.Total);
        if (size < OpenSubtitlesHashChunkSize * 2)
        {
            throw std::runtime_error{ "The source is too small for moviehash." };
        }
        auto const tail = co_await ReadRange(
            url,
            headers,
            executor,
            { size - OpenSubtitlesHashChunkSize, size - 1 },
            size);
        auto const hash = ComputeOpenSubtitlesMovieHash(size, head.Bytes, tail.Bytes);
        std::wostringstream text;
        text << std::hex << std::nouppercase << std::setw(16) << std::setfill(L'0') << hash;
        co_return VideoHashResult{ winrt::hstring{ text.str() }, size };
    }
}
