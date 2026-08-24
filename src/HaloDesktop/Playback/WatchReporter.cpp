#include "pch.h"
#include "Playback/WatchReporter.h"

#include "Api/Dto.h"
#include "Services/WatchStateService.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace
{
    std::int64_t NowMilliseconds()noexcept{return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
}

namespace HaloDesktop::Playback
{
    WatchReporter::WatchReporter(std::shared_ptr<Services::WatchStateService> watchState,winrt::HaloDesktop::PlaybackRequest request)
        :m_watchState(std::move(watchState)),m_request(std::move(request))
    {
        if(!m_watchState||!m_request)throw std::invalid_argument("WatchReporter requires watch state and a playback request.");
    }

    concurrency::task<void> WatchReporter::ReportAsync(PlaybackState state)
    {
        if(state.DurationSeconds<60.0||state.PositionSeconds<5.0)co_return;
        auto const position=std::floor(state.PositionSeconds),duration=std::floor(state.DurationSeconds);
        if(duration<=0)co_return;
        auto const timestamp=(std::max)(NowMilliseconds(),m_lastTimestamp+1);m_lastTimestamp=timestamp;
        auto name=m_request.ShowName().empty()?m_request.Title():m_request.ShowName();if(name.size()>512)name=winrt::hstring{name.c_str(),512};
        std::optional<winrt::hstring>poster;if(!m_request.Poster().empty())poster=m_request.Poster();
        co_await m_watchState->PutAsync({m_request.VideoId(),m_request.ItemId(),position,duration,position/duration>=0.9,name,poster,timestamp});
    }
}
