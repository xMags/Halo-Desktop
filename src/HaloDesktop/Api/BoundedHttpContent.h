#pragma once

#include <ppltasks.h>
#include <pplawait.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace HaloDesktop::Api
{
    [[nodiscard]] concurrency::task<winrt::hstring> ReadBoundedJsonTextAsync(
        winrt::Windows::Web::Http::IHttpContent const& content);

    // The whole body, refused with std::length_error once it passes the bound,
    // whether the server declared its length or not.
    [[nodiscard]] concurrency::task<std::vector<std::uint8_t>> ReadBoundedBytesAsync(
        winrt::Windows::Web::Http::IHttpContent const& content,
        std::size_t maximumBytes);
}
