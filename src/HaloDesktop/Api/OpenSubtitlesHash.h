#pragma once

#include <cstdint>
#include <memory>
#include <ppltasks.h>
#include <pplawait.h>
#include <winrt/Windows.Foundation.h>

namespace HaloDesktop::Api
{
    class HttpExecutor;
    struct VideoHashResult final{winrt::hstring Hash;std::uint64_t Size{};};
    [[nodiscard]] concurrency::task<VideoHashResult> ComputeRemoteVideoHashAsync(winrt::hstring url,std::shared_ptr<HttpExecutor> executor);
}
