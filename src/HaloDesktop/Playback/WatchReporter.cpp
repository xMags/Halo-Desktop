#include "pch.h"
#include "Playback/WatchReporter.h"

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

    std::optional<Api::Dto::WatchEntry> WatchReporter::RowFor(PlaybackState const& state)
    {
        if(state.DurationSeconds<60.0||state.PositionSeconds<5.0)return std::nullopt;
        auto const position=std::floor(state.PositionSeconds),duration=std::floor(state.DurationSeconds);
        if(duration<=0)return std::nullopt;
        // Drawn from the watch-state service, so a report is stamped after any
        // history removal made before it and a later removal outranks it.
        auto const timestamp=m_watchState->IssueTimestamp((std::max)(NowMilliseconds(),m_lastTimestamp+1));m_lastTimestamp=timestamp;
        auto name=m_request.ShowName().empty()?m_request.Title():m_request.ShowName();if(name.size()>512)name=winrt::hstring{name.c_str(),512};
        std::optional<winrt::hstring>poster;if(!m_request.Poster().empty())poster=m_request.Poster();
        return Api::Dto::WatchEntry{m_request.VideoId(),m_request.ItemId(),position,duration,position/duration>=0.9,name,poster,timestamp};
    }

    concurrency::task<void> WatchReporter::ReportAsync(PlaybackState state)
    {
        auto row=RowFor(state);
        if(!row)co_return;
        m_lastStaged=*row;
        co_await m_watchState->ReportProgressAsync(std::move(*row));
    }

    void WatchReporter::Stage(PlaybackState const& state)
    {
        // Compared before a stamp is drawn, so a paused player does not rewrite
        // the file every two seconds with the same place under a new time.
        if(state.DurationSeconds<60.0||state.PositionSeconds<5.0)return;
        auto const position=std::floor(state.PositionSeconds),duration=std::floor(state.DurationSeconds);
        if(m_lastStaged&&m_lastStaged->PositionSec==position&&m_lastStaged->DurationSec==duration)return;
        auto row=RowFor(state);
        if(!row)return;
        m_lastStaged=*row;
        m_watchState->StageProgress(std::move(*row));
    }
}
