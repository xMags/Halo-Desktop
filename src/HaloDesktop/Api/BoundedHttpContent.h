#pragma once

#include <ppltasks.h>
#include <pplawait.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>

namespace HaloDesktop::Api
{
    [[nodiscard]] concurrency::task<winrt::hstring> ReadBoundedJsonTextAsync(
        winrt::Windows::Web::Http::IHttpContent const& content);
}
