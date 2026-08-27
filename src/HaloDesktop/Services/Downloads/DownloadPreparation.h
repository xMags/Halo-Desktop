#pragma once

#include "Services/Downloads/DownloadTypes.h"

#include <functional>
#include <optional>
#include <pplawait.h>
#include <ppltasks.h>
#include <utility>

namespace HaloDesktop::Services::Downloads
{
    using PreparedSubtitleTask = concurrency::task<std::optional<SubtitleRequest>>;
    using SubtitlePreparation = std::function<PreparedSubtitleTask()>;

    // Subtitle lookup is an enhancement to an otherwise valid video request.
    // Any failure returns that video request unchanged, including its protected
    // headers, so a sidecar service cannot prevent offline video playback.
    [[nodiscard]] inline concurrency::task<DownloadStartRequest>
        PrepareDownloadWithOptionalSubtitleAsync(
            DownloadStartRequest request,
            SubtitlePreparation prepareSubtitle)
    {
        if (!prepareSubtitle)
        {
            co_return request;
        }

        try
        {
            auto subtitle = co_await prepareSubtitle();
            if (subtitle)
            {
                request.Request.Subtitle = std::move(*subtitle);
            }
        }
        catch (...)
        {
            request.Request.Subtitle.reset();
        }
        co_return request;
    }
}
