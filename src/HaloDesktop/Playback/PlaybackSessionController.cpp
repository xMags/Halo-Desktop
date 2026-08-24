#include "pch.h"
#include "Playback/PlaybackSessionController.h"

#include "Playback/WatchReporter.h"
#include "Services/PlaybackPreferences.h"
#include "Services/WatchStateService.h"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace HaloDesktop::Playback
{
    PlaybackSessionController::PlaybackSessionController(std::shared_ptr<IPlaybackEngine>engine,std::shared_ptr<Services::WatchStateService>watchState):m_engine(std::move(engine)),m_watchState(std::move(watchState))
    {if(!m_engine||!m_watchState)throw std::invalid_argument("PlaybackSessionController requires its dependencies.");}
    PlaybackSessionController::~PlaybackSessionController(){Stop();}

    concurrency::task<void> PlaybackSessionController::StartAsync(winrt::HaloDesktop::PlaybackRequest request)
    {
        if(!request||request.Url().empty())throw std::invalid_argument("A playback request with a source is required.");
        auto const version=++m_startVersion;auto const ui=winrt::apartment_context{};m_request=request;
        try{co_await m_watchState->LoadAsync();}catch(...){}
        co_await ui;if(version!=m_startVersion||m_closing)co_return;
        m_prior=m_watchState->Find(request.VideoId());m_reporter=std::make_shared<WatchReporter>(m_watchState,request);
        auto const dispatcher=winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();if(!dispatcher)throw winrt::hresult_wrong_thread();
        m_reportTimer=dispatcher.CreateTimer();m_reportTimer.Interval(std::chrono::seconds(15));m_reportTimer.IsRepeating(true);
        m_reportTickRevoker=m_reportTimer.Tick(winrt::auto_revoke,[weak=weak_from_this()](auto const&,auto const&){if(auto self=weak.lock())self->ReportNow();});
        m_lastState=m_engine->State();m_seenFileSerial=m_lastState.FileSerial;m_seenEndSerial=m_lastState.EndSerial;
        m_engineToken=m_engine->AddChangedHandler([weak=weak_from_this()](){if(auto self=weak.lock())self->OnEngineChanged();});m_started=true;
        m_engine->Open(std::wstring(request.Url().c_str()));m_engine->SetPaused(false);
    }

    concurrency::task<void> PlaybackSessionController::CloseAsync()
    {
        if(m_closing)co_return;m_closing=true;++m_startVersion;
        co_await ReportWithTimeoutAsync();Stop();
    }

    void PlaybackSessionController::Stop()noexcept
    {
        ++m_startVersion;m_started=false;
        try{if(m_reportTimer)m_reportTimer.Stop();}catch(...){}
        m_reportTickRevoker.revoke();m_reportTimer=nullptr;
        if(m_engineToken){m_engine->RemoveChangedHandler(m_engineToken);m_engineToken=0;}
        m_reporter.reset();m_prior.reset();
    }

    void PlaybackSessionController::SetErrorHandler(std::function<void()>handler){m_errorHandler=std::move(handler);}

    void PlaybackSessionController::OnEngineChanged()
    {
        if(!m_started)return;auto const state=m_engine->State();
        if(state.FileSerial!=m_seenFileSerial){m_seenFileSerial=state.FileSerial;ApplyResume();}
        if(state.EndSerial!=m_seenEndSerial){m_seenEndSerial=state.EndSerial;ReportNow();if(state.EndReason==PlaybackEndReason::Error&&m_errorHandler)m_errorHandler();}
        auto const wasPlaying=IsPlaying(m_lastState),playing=IsPlaying(state);if(wasPlaying&&!playing)ReportNow();
        if(playing&&!wasPlaying&&m_reportTimer)m_reportTimer.Start();else if(!playing&&wasPlaying&&m_reportTimer)m_reportTimer.Stop();
        m_lastState=state;
    }

    void PlaybackSessionController::ReportNow()noexcept
    {
        if(!m_reporter)return;auto reporter=m_reporter;auto state=m_engine->State();
        reporter->ReportAsync(state).then([](concurrency::task<void> task){try{task.get();}catch(...){}});
    }

    concurrency::task<void> PlaybackSessionController::ReportWithTimeoutAsync()
    {
        if(!m_reporter)co_return;
        concurrency::task_completion_event<void>completion;auto reporter=m_reporter;auto state=m_engine->State();
        reporter->ReportAsync(state).then([completion](concurrency::task<void>task)mutable{try{task.get();}catch(...){}completion.set();});
        auto const dispatcher=winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();if(!dispatcher)co_return;
        auto timer=dispatcher.CreateTimer();timer.Interval(std::chrono::seconds(2));timer.IsRepeating(false);timer.Tick([completion](auto const&,auto const&)mutable{completion.set();});timer.Start();
        co_await concurrency::create_task(completion);timer.Stop();
    }

    bool PlaybackSessionController::IsPlaying(PlaybackState const&state)const noexcept{return state.FileSerial>0&&!state.Paused&&!state.Buffering&&state.EndReason==PlaybackEndReason::None;}

    void PlaybackSessionController::ApplyResume()
    {
        if(!Services::PlaybackPreferences::ResumeEnabled()||!m_prior||m_prior->Watched||m_prior->PositionSec<=30)return;
        auto const duration=m_engine->DurationNow();if(duration>0&&m_prior->PositionSec/duration<0.95)m_engine->SeekAbsolute(m_prior->PositionSec);
        m_prior.reset();
    }
}
