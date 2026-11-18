#include "pch.h"
#include "Api/BoundedHttpContent.h"

#include "Api/ResponseSizePolicy.h"

#include <algorithm>
#include <climits>
#include <optional>
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

    // Cancels one read while it is in flight, for as long as it lives. With a
    // token that cannot be cancelled it does nothing.
    class CancelWhileReading final
    {
    public:
        CancelWhileReading(
            concurrency::cancellation_token const& cancellation,
            winrt::Windows::Foundation::IAsyncInfo const& operation)
            : m_cancellation(cancellation)
        {
            if (!m_cancellation.is_cancelable()) return;
            m_registration = m_cancellation.register_callback([operation]()
            {
                try { operation.Cancel(); } catch (...) {}
            });
        }

        ~CancelWhileReading()
        {
            if (!m_registration) return;
            try { m_cancellation.deregister_callback(*m_registration); } catch (...) {}
        }

        CancelWhileReading(CancelWhileReading const&) = delete;
        CancelWhileReading& operator=(CancelWhileReading const&) = delete;

    private:
        concurrency::cancellation_token m_cancellation;
        std::optional<concurrency::cancellation_token_registration> m_registration;
    };
}

namespace HaloDesktop::Api
{
    concurrency::task<winrt::hstring> ReadBoundedJsonTextAsync(
        winrt::Windows::Web::Http::IHttpContent const& content)
    {
        auto const bytes = co_await ReadBoundedBytesAsync(content, MaximumJsonResponseBytes);
        co_return DecodeUtf8(bytes);
    }

    concurrency::task<winrt::hstring> ReadBoundedJsonTextAsync(
        winrt::Windows::Web::Http::IHttpContent const& content,
        concurrency::cancellation_token cancellation)
    {
        auto const bytes = co_await ReadBoundedBytesAsync(content, MaximumJsonResponseBytes, cancellation);
        co_return DecodeUtf8(bytes);
    }

    concurrency::task<std::vector<std::uint8_t>> ReadBoundedBytesAsync(
        winrt::Windows::Web::Http::IHttpContent const& content,
        std::size_t maximumBytes)
    {
        return ReadBoundedBytesAsync(content, maximumBytes, concurrency::cancellation_token::none());
    }

    concurrency::task<std::vector<std::uint8_t>> ReadBoundedBytesAsync(
        winrt::Windows::Web::Http::IHttpContent const& content,
        std::size_t maximumBytes,
        concurrency::cancellation_token cancellation)
    {
        // `content` is only read before the first suspension: the caller's
        // reference is not guaranteed to outlive it.
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
        ValidateDeclaredResponseSize(declaredBytes, maximumBytes);
        if (cancellation.is_canceled())
        {
            throw winrt::hresult_canceled{};
        }

        auto const opening = content.ReadAsInputStreamAsync();
        winrt::Windows::Storage::Streams::IInputStream input{ nullptr };
        {
            CancelWhileReading const guard{ cancellation, opening };
            input = co_await opening;
        }
        winrt::Windows::Storage::Streams::DataReader reader{ input };
        reader.InputStreamOptions(winrt::Windows::Storage::Streams::InputStreamOptions::Partial);

        std::vector<std::uint8_t> bytes;
        if (declaredBytes)
        {
            bytes.reserve(static_cast<std::size_t>(*declaredBytes));
        }
        for (;;)
        {
            auto const probeBytes = maximumBytes + 1u - bytes.size();
            auto const requested = static_cast<std::uint32_t>((std::min)(
                probeBytes,
                static_cast<std::size_t>(ReadChunkBytes)));
            auto const load = reader.LoadAsync(requested);
            std::uint32_t loaded{};
            {
                CancelWhileReading const guard{ cancellation, load };
                loaded = co_await load;
            }
            if (loaded == 0)
            {
                break;
            }

            auto const nextSize = CheckedResponseSize(bytes.size(), loaded, maximumBytes);
            auto const offset = bytes.size();
            bytes.resize(nextSize);
            reader.ReadBytes(winrt::array_view<std::uint8_t>{
                bytes.data() + offset,
                bytes.data() + nextSize,
            });
        }
        co_return bytes;
    }
}
