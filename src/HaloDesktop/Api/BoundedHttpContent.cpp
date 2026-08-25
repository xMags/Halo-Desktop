#include "pch.h"
#include "Api/BoundedHttpContent.h"

#include "Api/ResponseSizePolicy.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.Headers.h>

namespace
{
    constexpr std::uint32_t ReadChunkBytes = 64u * 1024u;

    winrt::hstring DecodeUtf8(std::vector<std::uint8_t> const& bytes)
    {
        std::size_t offset{};
        if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
        {
            offset = 3;
        }
        auto const byteCount = bytes.size() - offset;
        if (byteCount == 0)
        {
            return {};
        }
        if (byteCount > static_cast<std::size_t>(INT_MAX))
        {
            throw std::length_error{ "The server response exceeded the size limit." };
        }

        // MultiByteToWideChar consumes raw UTF-8 bytes through a char pointer.
        auto const input = reinterpret_cast<char const*>(bytes.data() + offset);
        auto const inputSize = static_cast<int>(byteCount);
        auto const characterCount = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            input,
            inputSize,
            nullptr,
            0);
        if (characterCount <= 0)
        {
            throw std::invalid_argument{ "The server returned invalid UTF-8." };
        }

        std::wstring result(static_cast<std::size_t>(characterCount), L'\0');
        if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            input,
            inputSize,
            result.data(),
            characterCount) != characterCount)
        {
            throw std::invalid_argument{ "The server response could not be decoded." };
        }
        return winrt::hstring{ result };
    }
}

namespace HaloDesktop::Api
{
    concurrency::task<winrt::hstring> ReadBoundedJsonTextAsync(
        winrt::Windows::Web::Http::IHttpContent const& content)
    {
        if (!content)
        {
            throw std::invalid_argument{ "The server returned no response body." };
        }

        auto const declaredLength = content.Headers().ContentLength();
        std::optional<std::uint64_t> declaredBytes;
        if (declaredLength)
        {
            declaredBytes = declaredLength.Value();
        }
        ValidateDeclaredResponseSize(declaredBytes);

        auto input = co_await content.ReadAsInputStreamAsync();
        winrt::Windows::Storage::Streams::DataReader reader{ input };
        reader.InputStreamOptions(winrt::Windows::Storage::Streams::InputStreamOptions::Partial);

        std::vector<std::uint8_t> bytes;
        if (declaredBytes)
        {
            bytes.reserve(static_cast<std::size_t>(*declaredBytes));
        }
        for (;;)
        {
            auto const probeBytes = MaximumJsonResponseBytes + 1u - bytes.size();
            auto const requested = static_cast<std::uint32_t>((std::min)(
                probeBytes,
                static_cast<std::size_t>(ReadChunkBytes)));
            auto const loaded = co_await reader.LoadAsync(requested);
            if (loaded == 0)
            {
                break;
            }

            auto const nextSize = CheckedResponseSize(bytes.size(), loaded);
            auto const offset = bytes.size();
            bytes.resize(nextSize);
            reader.ReadBytes(winrt::array_view<std::uint8_t>{
                bytes.data() + offset,
                bytes.data() + nextSize,
            });
        }
        co_return DecodeUtf8(bytes);
    }
}
