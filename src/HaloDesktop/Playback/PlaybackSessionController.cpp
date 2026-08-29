#include "pch.h"
#include "Playback/PlaybackSessionController.h"

#include "Models/Models.h"
#include "Playback/PlaybackPolicy.h"
#include "Playback/WatchReporter.h"
#include "Services/PlaybackPreferences.h"
#include "Services/SettingsSyncService.h"
#include "Services/WatchStateService.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace HaloDesktop::Playback
{
    PlaybackSessionController::PlaybackSessionController(std::shared_ptr<IPlaybackEngine>engine,std::shared_ptr<Services::WatchStateService>watchState,std::shared_ptr<Services::SettingsSyncService>settings,std::shared_ptr<Services::PlaybackPreferences>preferences,std::shared_ptr<IScrubPreviewSource>scrubPreview):m_engine(std::move(engine)),m_watchState(std::move(watchState)),m_settings(std::move(settings)),m_preferences(std::move(preferences)),m_scrubPreview(std::move(scrubPreview))
    {if(!m_engine||!m_watchState||!m_settings||!m_preferences||!m_scrubPreview)throw std::invalid_argument("PlaybackSessionController requires its dependencies.");}
    PlaybackSessionController::~PlaybackSessionController(){Stop();}

    concurrency::task<void> PlaybackSessionController::StartAsync(winrt::HaloDesktop::PlaybackRequest request)
    {
        if(!request||request.Url().empty())throw std::invalid_argument("A playback request with a source is required.");
        auto const version=++m_startVersion;m_request=request;m_prior=m_watchState->Find(request.VideoId());m_reporter=std::make_shared<WatchReporter>(m_watchState,request);
        auto const dispatcher=winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();if(!dispatcher)throw winrt::hresult_wrong_thread();
        m_reportTimer=dispatcher.CreateTimer();m_reportTimer.Interval(std::chrono::seconds(15));m_reportTimer.IsRepeating(true);
        m_reportTickRevoker=m_reportTimer.Tick(winrt::auto_revoke,[weak=weak_from_this()](auto const&,auto const&){if(auto self=weak.lock())self->ReportNow();});
        m_lastState=m_engine->State();m_seenFileSerial=m_lastState.FileSerial;m_seenEndSerial=m_lastState.EndSerial;m_initialSeekSerial=m_lastState.SeekSerial;m_initialAudioSelectionSerial=m_lastState.AudioSelectionSerial;m_autoAudioSelectionSerial=m_initialAudioSelectionSerial;m_resumeDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);m_fileReady=false;m_watchLoadFinished=false;
        m_engineToken=m_engine->AddChangedHandler([weak=weak_from_this()](){if(auto self=weak.lock())self->OnEngineChanged();});m_started=true;
        RefreshPreferences();
        PlaybackSource source{.Location=std::wstring(request.Url().c_str())};
        auto const implementation=winrt::get_self<winrt::HaloDesktop::implementation::PlaybackRequest>(request);
        source.Headers.reserve(implementation->RequestHeaders().size());
        for(auto const&[name,value]:implementation->RequestHeaders())source.Headers.push_back({std::wstring(name.c_str()),std::wstring(value.c_str())});
        try{m_scrubPreview->Open(source);m_engine->Open(std::move(source));m_engine->SetPaused(false);}
        catch(...){Stop();throw;}
        LoadWatchStateAsync(version).then([](concurrency::task<void>task){try{task.get();}catch(...){}});
        co_return;
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
        m_reporter.reset();m_prior.reset();m_fileReady=false;
        m_scrubPreview->Close();
    }

    void PlaybackSessionController::SetErrorHandler(std::function<void()>handler){m_errorHandler=std::move(handler);}
    void PlaybackSessionController::SetEndOfFileHandler(std::function<void()>handler){m_endOfFileHandler=std::move(handler);}
    void PlaybackSessionController::RefreshPreferences(){m_preferredAudio=m_settings->PreferredAudioLanguage();++m_audioPreferenceRevision;ApplyAudioPreference();}

    void PlaybackSessionController::OnEngineChanged()
    {
        if(!m_started)return;auto state=m_engine->State();
        if(state.FileSerial!=m_seenFileSerial){m_seenFileSerial=state.FileSerial;m_fileReady=true;ApplyResume();state=m_engine->State();}
        auto const endChanged=state.EndSerial!=m_seenEndSerial;
        if(endChanged){m_seenEndSerial=state.EndSerial;if(state.EndReason==PlaybackEndReason::Error&&m_errorHandler)m_errorHandler();else if(state.EndReason==PlaybackEndReason::Eof&&m_endOfFileHandler)m_endOfFileHandler();}
        auto const wasPlaying=IsPlaying(m_lastState),playing=IsPlaying(state);
        if(ShouldReportPlaybackChange(endChanged,wasPlaying,playing))ReportNow();
        if(playing&&!wasPlaying&&m_reportTimer)m_reportTimer.Start();else if(!playing&&wasPlaying&&m_reportTimer)m_reportTimer.Stop();
        ApplyAudioPreference();m_lastState=m_engine->State();
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

    concurrency::task<void> PlaybackSessionController::LoadWatchStateAsync(std::uint64_t version)
    {
        auto lifetime=shared_from_this();auto const ui=winrt::apartment_context{};try{co_await m_watchState->LoadAsync();}catch(...){}co_await ui;
        if(version!=m_startVersion||m_closing)co_return;m_watchLoadFinished=true;m_prior=m_watchState->Find(m_request.VideoId());ApplyResume();
    }

    bool PlaybackSessionController::IsPlaying(PlaybackState const&state)const noexcept{return state.FileSerial>0&&!state.Paused&&!state.Buffering&&state.EndReason==PlaybackEndReason::None;}

    void PlaybackSessionController::ApplyResume()
    {
        if(!m_fileReady||!m_prior)return;auto const state=m_engine->State();auto const withinWindow=std::chrono::steady_clock::now()<=m_resumeDeadline;
        if(ShouldApplyResume(m_preferences->ResumeEnabled(),m_prior->Watched,m_prior->PositionSec,m_engine->DurationNow(),state.PositionSeconds,state.SeekSerial,m_initialSeekSerial,withinWindow))m_engine->SeekAbsolute(static_cast<double>(m_prior->PositionSec));
        if(m_watchLoadFinished||!withinWindow||m_prior->Watched||m_prior->PositionSec<=30)m_prior.reset();
    }

    void PlaybackSessionController::ApplyAudioPreference()
    {
        if(!m_started||!m_fileReady||!m_preferredAudio)return;auto const state=m_engine->State();
        if(!CanApplyAutomaticSelection(state.AudioSelectionSerial,m_initialAudioSelectionSerial,m_autoAudioSelectionSerial))return;
        if(m_appliedAudioFileSerial==state.FileSerial&&m_appliedAudioPreferenceRevision==m_audioPreferenceRevision)return;
        auto const track=FindLanguageTrack(state.Tracks,TrackType::Audio,std::wstring(m_preferredAudio->c_str()),false);if(!track)return;
        auto const selected=std::find_if(state.Tracks.begin(),state.Tracks.end(),[](TrackInfo const&value){return value.Type==TrackType::Audio&&value.Selected;});
        if(selected==state.Tracks.end()||selected->Id!=*track){m_engine->SetAudioTrack(*track);m_autoAudioSelectionSerial=m_engine->State().AudioSelectionSerial;}
        m_appliedAudioFileSerial=state.FileSerial;m_appliedAudioPreferenceRevision=m_audioPreferenceRevision;
    }
}
