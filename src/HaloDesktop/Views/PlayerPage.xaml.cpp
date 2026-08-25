#include "pch.h"
#include "Views/PlayerPage.xaml.h"
#if __has_include("PlayerPage.g.cpp")
#include "PlayerPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Controls/PlayerOsd.xaml.h"
#include "Controls/VideoHostControl.xaml.h"
#include "Playback/PlaybackSessionController.h"
#include "Playback/SubtitleController.h"
#include "Services/NavigationService.h"
#include "Services/SettingsSyncService.h"
#include "Shell/WindowPresentationService.h"
#include "ViewModels/PlayerViewModel.h"

#include <winrt/Microsoft.UI.Dispatching.h>
#include <pplawait.h>

namespace winrt::HaloDesktop::implementation
{
    PlayerPage::PlayerPage()=default;
    winrt::HaloDesktop::PlayerViewModel PlayerPage::ViewModel()const{return m_viewModel;}
    void PlayerPage::OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const&args){m_request=args.Parameter().try_as<winrt::HaloDesktop::PlaybackRequest>();}

    void PlayerPage::OnLoaded(winrt::Windows::Foundation::IInspectable const&,Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_loaded=true;m_closing=false;
        auto const videoHost=FindName(L"VideoHost").as<winrt::HaloDesktop::VideoHostControl>();winrt::get_self<VideoHostControl>(videoHost)->EnsureHostWindow();
        auto const overlay=FindName(L"PlayerOverlay").as<winrt::HaloDesktop::PlayerOsd>();m_viewModel=overlay.ViewModel();auto const viewModel=winrt::get_self<PlayerViewModel>(m_viewModel);
        viewModel->SetCloseRequestedHandler([weak=get_weak()](){if(auto self=weak.get())self->BeginClose();});
        viewModel->SetPlayNextHandler([weak=get_weak()](){if(auto self=weak.get())self->AdvanceUpNextAsync();});
        auto const subtitles=App::Services().Subtitles;
        viewModel->SetSubtitleTrackHandler([subtitles](std::int64_t id){subtitles->SelectTrack(id);});
        viewModel->SetSubtitlesOffHandler([subtitles](){subtitles->Disable();});
        viewModel->SetAddonSubtitleHandler([subtitles](winrt::hstring key){subtitles->SelectAsync(std::move(key)).then([](concurrency::task<void>task){try{task.get();}catch(...){}});});
        m_presentationChangedToken=m_viewModel.PropertyChanged([weak=get_weak()](auto const&,Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const&args){if(args.PropertyName()==L"IsFullscreen")if(auto self=weak.get())self->RefreshOverlayAfterPresentationChange();});
        UpdateOverlayLayout();
        try{viewModel->Activate();InitializePlaybackAsync();}
        catch(...){ShowMediaPrompt(L"The player could not start this source.");}
    }

    void PlayerPage::OnUnloaded(winrt::Windows::Foundation::IInspectable const&,Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_loaded=false;
        if(m_viewModel&&m_presentationChangedToken.value!=0){m_viewModel.PropertyChanged(m_presentationChangedToken);m_presentationChangedToken={};}
        CloseOverlayPopup(true);
        if(m_session)m_session->Stop();App::Services().Subtitles->Stop();
        if(m_viewModel){auto vm=winrt::get_self<PlayerViewModel>(m_viewModel);vm->SetCloseRequestedHandler({});vm->SetPlayNextHandler({});vm->SetSubtitleTrackHandler({});vm->SetSubtitlesOffHandler({});vm->SetAddonSubtitleHandler({});vm->Deactivate();}
        App::Services().Subtitles->CleanupTemporaryFiles();
        auto const videoHost=FindName(L"VideoHost").as<winrt::HaloDesktop::VideoHostControl>();winrt::get_self<VideoHostControl>(videoHost)->DestroyHostWindow();
        m_session.reset();
    }

    winrt::fire_and_forget PlayerPage::InitializePlaybackAsync()
    {
        auto lifetime=get_strong();
        if(!m_request){ShowMediaPrompt(L"This source is no longer available. Return to Sources and choose it again.");co_return;}
        co_await StartRequestAsync(m_request);
    }

    winrt::Windows::Foundation::IAsyncAction PlayerPage::StartRequestAsync(winrt::HaloDesktop::PlaybackRequest request)
    {
        auto lifetime=get_strong();auto const uiContext=winrt::apartment_context{};auto const generation=++m_playbackGeneration;m_request=request;m_upNext.reset();m_advancing=false;
        auto&services=App::Services();auto const viewModel=winrt::get_self<PlayerViewModel>(m_viewModel);viewModel->SetUpNext(L"",L"",L"");
        auto const overlay=FindName(L"PlayerOverlay").as<winrt::HaloDesktop::PlayerOsd>();winrt::get_self<PlayerOsd>(overlay)->TitleLabel(request.ShowName().empty()?request.Title():request.ShowName());
        auto line=request.EpisodeLabel();if(!line.empty()&&!request.SourceTagLine().empty())line=line+L" \x00B7 ";line=line+request.SourceTagLine();winrt::get_self<PlayerOsd>(overlay)->SourceLabel(line);
        FindName(L"MediaPrompt").as<Microsoft::UI::Xaml::Controls::Border>().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        FindName(L"SubtitleNotice").as<Microsoft::UI::Xaml::Controls::InfoBar>().IsOpen(false);

        auto session=std::make_shared<::HaloDesktop::Playback::PlaybackSessionController>(services.Playback,services.WatchState,services.SettingsSync);m_session=session;
        session->SetErrorHandler([weak=get_weak(),generation](){if(auto self=weak.get();self&&generation==self->m_playbackGeneration)self->ShowMediaPrompt(L"Playback failed for this source. Return to Sources and choose another one.");});
        session->SetEndOfFileHandler([weak=get_weak(),generation](){if(auto self=weak.get();self&&generation==self->m_playbackGeneration&&self->m_upNext&&self->m_viewModel)winrt::get_self<PlayerViewModel>(self->m_viewModel)->BeginUpNextCountdown();});
        services.Subtitles->SetChoicesChangedHandler([weak=get_weak(),generation](){if(auto self=weak.get();self&&generation==self->m_playbackGeneration&&self->m_viewModel)winrt::get_self<PlayerViewModel>(self->m_viewModel)->SetAddonSubtitles(App::Services().Subtitles->Choices());});
        services.Subtitles->SetErrorHandler([weak=get_weak(),generation](){if(auto self=weak.get();self&&self->m_loaded&&!self->m_closing&&generation==self->m_playbackGeneration){auto const queued=self->DispatcherQueue().TryEnqueue([weak,generation](){if(auto current=weak.get();current&&current->m_loaded&&!current->m_closing&&generation==current->m_playbackGeneration)current->ShowSubtitleError();});static_cast<void>(queued);}});
        PrefetchUpNextAsync(request,generation);
        try{co_await session->StartAsync(request);}
        catch(...){if(m_loaded&&generation==m_playbackGeneration)ShowMediaPrompt(L"The player could not start this source. Return to Sources and try another one.");co_return;}
        co_await uiContext;
        if(!m_loaded||m_closing||generation!=m_playbackGeneration)co_return;
        services.Subtitles->PrepareAsync(request).then([](concurrency::task<void>task){try{task.get();}catch(...){}});
        RefreshPlaybackSettingsAsync(generation);
    }

    winrt::fire_and_forget PlayerPage::RefreshPlaybackSettingsAsync(std::uint64_t generation)
    {
        auto lifetime=get_strong();auto const uiContext=winrt::apartment_context{};
        try{co_await App::Services().SettingsSync->LoadAsync();}catch(...){}
        co_await uiContext;
        if(!m_loaded||m_closing||generation!=m_playbackGeneration)co_return;
        if(m_session)m_session->RefreshPreferences();
        App::Services().Subtitles->RefreshPreferences();
    }

    winrt::fire_and_forget PlayerPage::PrefetchUpNextAsync(winrt::HaloDesktop::PlaybackRequest request,std::uint64_t generation)
    {
        auto lifetime=get_strong();auto const uiContext=winrt::apartment_context{};auto result=co_await App::Services().UpNext->ResolveAsync(request);co_await uiContext;
        if(!m_loaded||m_closing||generation!=m_playbackGeneration||!result)co_return;
        m_upNext=std::move(result);auto viewModel=winrt::get_self<PlayerViewModel>(m_viewModel);auto const poster=m_upNext->Playback?m_upNext->Playback.Poster():(m_upNext->Sources?m_upNext->Sources.Poster():L"");viewModel->SetUpNext(m_upNext->Title,m_upNext->EpisodeLabel,poster);
        if(App::Services().Playback->State().EndReason==::HaloDesktop::Playback::PlaybackEndReason::Eof)viewModel->BeginUpNextCountdown();
    }

    winrt::fire_and_forget PlayerPage::AdvanceUpNextAsync()
    {
        auto lifetime=get_strong();auto const uiContext=winrt::apartment_context{};if(m_advancing||m_closing||!m_upNext)co_return;m_advancing=true;auto next=*m_upNext;auto currentSession=m_session;
        if(currentSession)co_await currentSession->CloseAsync();
        co_await uiContext;
        if(!m_loaded||m_closing)co_return;
        App::Services().Subtitles->Stop();
        if(next.Playback){co_await StartRequestAsync(next.Playback);co_return;}
        co_await PrepareForWindowCloseAsync();
        auto navigation=App::Services().Navigation;navigation->CloseOverlay();navigation->GoTo(::HaloDesktop::Services::Page::Sources,next.Sources);
    }

    winrt::fire_and_forget PlayerPage::BeginClose()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        if (m_closing) co_return;
        try
        {
            co_await PrepareForWindowCloseAsync();
        }
        catch (...)
        {
            // A reporting or teardown failure must never strand the modal player.
        }
        co_await uiContext;
        if (m_loaded) App::Services().Navigation->CloseOverlay();
    }

    winrt::Windows::Foundation::IAsyncAction PlayerPage::PrepareForWindowCloseAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        if (m_closing) co_return;
        m_closing = true;
        ++m_playbackGeneration;
        try
        {
            if (m_session) co_await m_session->CloseAsync();
        }
        catch (...)
        {
            // The final report is best-effort. Local engine teardown still has to run.
        }
        co_await uiContext;
        if (!m_loaded) co_return;

        CloseOverlayPopup(true);
        App::Services().Subtitles->Stop();
        if (m_viewModel) winrt::get_self<PlayerViewModel>(m_viewModel)->Deactivate();
        App::Services().Subtitles->CleanupTemporaryFiles();
        auto const videoHost = FindName(L"VideoHost").as<winrt::HaloDesktop::VideoHostControl>();
        winrt::get_self<VideoHostControl>(videoHost)->DestroyHostWindow();
    }

    void PlayerPage::ShowMediaPrompt(winrt::hstring const&message){FindName(L"MediaPromptMessage").as<Microsoft::UI::Xaml::Controls::TextBlock>().Text(message);FindName(L"MediaPrompt").as<Microsoft::UI::Xaml::Controls::Border>().Visibility(Microsoft::UI::Xaml::Visibility::Visible);}
    void PlayerPage::ShowSubtitleError(){FindName(L"SubtitleNotice").as<Microsoft::UI::Xaml::Controls::InfoBar>().IsOpen(true);}
    void PlayerPage::OnCloseErrorClick(winrt::Windows::Foundation::IInspectable const&,Microsoft::UI::Xaml::RoutedEventArgs const&){BeginClose();}
    void PlayerPage::OnPlayerSizeChanged(winrt::Windows::Foundation::IInspectable const&,Microsoft::UI::Xaml::SizeChangedEventArgs const&){UpdateOverlayLayout();}
    void PlayerPage::OnSpaceInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&args){m_viewModel.TogglePause();CompleteKeyboardAction(args);}
    void PlayerPage::OnLeftInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&args){m_viewModel.SeekRelative(-10.0);CompleteKeyboardAction(args);}
    void PlayerPage::OnRightInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&args){m_viewModel.SeekRelative(10.0);CompleteKeyboardAction(args);}
    void PlayerPage::OnUpInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&args){m_viewModel.ChangeVolume(5.0);CompleteKeyboardAction(args);}
    void PlayerPage::OnDownInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&args){m_viewModel.ChangeVolume(-5.0);CompleteKeyboardAction(args);}
    void PlayerPage::OnFullscreenInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&args){m_viewModel.ToggleFullscreen();CompleteKeyboardAction(args);}
    void PlayerPage::OnEscapeInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&args){m_viewModel.HandleEscape();CompleteKeyboardAction(args);}

    void PlayerPage::OnOverlayPreviewKeyDown(winrt::Windows::Foundation::IInspectable const&,Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&args)
    {
        using winrt::Windows::System::VirtualKey;if(!m_viewModel)return;
        switch(args.Key()){case VirtualKey::Space:m_viewModel.TogglePause();break;case VirtualKey::Left:m_viewModel.SeekRelative(-10.0);break;case VirtualKey::Right:m_viewModel.SeekRelative(10.0);break;case VirtualKey::Up:m_viewModel.ChangeVolume(5.0);break;case VirtualKey::Down:m_viewModel.ChangeVolume(-5.0);break;case VirtualKey::F:m_viewModel.ToggleFullscreen();break;case VirtualKey::Escape:m_viewModel.HandleEscape();break;default:return;}
        m_viewModel.NotifyUserActivity();args.Handled(true);
    }
    void PlayerPage::CompleteKeyboardAction(Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&args){if(m_viewModel)m_viewModel.NotifyUserActivity();args.Handled(true);}

    void PlayerPage::CloseOverlayPopup(bool detachChild) noexcept
    {
        try
        {
            auto const popup = FindName(L"OverlayPopup").try_as<Microsoft::UI::Xaml::Controls::Primitives::Popup>();
            if (!popup)
            {
                return;
            }
            popup.IsOpen(false);
            if (detachChild)
            {
                popup.Child(nullptr);
            }
        }
        catch (...)
        {
        }
    }

    void PlayerPage::UpdateOverlayLayout()
    {
        auto const root = PlayerRoot();
        if (!m_loaded || m_closing || root.ActualWidth() <= 0.0 || root.ActualHeight() <= 0.0)
        {
            CloseOverlayPopup(false);
            return;
        }

        auto const popup = OverlayPopup();
        auto const overlay = OverlayHost();
        overlay.Width(root.ActualWidth());
        overlay.Height(root.ActualHeight());
        popup.HorizontalOffset(0.0);
        popup.VerticalOffset(0.0);
        if (!popup.IsOpen())
        {
            popup.IsOpen(true);
        }
        overlay.Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }
    void PlayerPage::RefreshOverlayAfterPresentationChange()
    {
        if(!m_loaded||m_closing)return;CloseOverlayPopup(false);auto const enqueued=DispatcherQueue().TryEnqueue(Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,[weak=get_weak()](){if(auto self=weak.get();self&&self->m_loaded&&!self->m_closing)self->UpdateOverlayLayout();});if(!enqueued)UpdateOverlayLayout();
    }
}
