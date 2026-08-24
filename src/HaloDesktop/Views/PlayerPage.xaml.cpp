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
        auto const subtitles=App::Services().Subtitles;viewModel->SetAddonSubtitleHandler([subtitles](winrt::hstring key){subtitles->SelectAsync(std::move(key)).then([](concurrency::task<void>task){try{task.get();}catch(...){}});});
        subtitles->SetChoicesChangedHandler([weak=get_weak()](){if(auto self=weak.get();self&&self->m_viewModel)winrt::get_self<PlayerViewModel>(self->m_viewModel)->SetAddonSubtitles(App::Services().Subtitles->Choices());});
        m_presentationChangedToken=m_viewModel.PropertyChanged([weak=get_weak()](auto const&,Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const&args){if(args.PropertyName()==L"IsFullscreen")if(auto self=weak.get())self->RefreshOverlayAfterPresentationChange();});
        if(m_request)
        {
            winrt::get_self<PlayerOsd>(overlay)->TitleLabel(m_request.ShowName().empty()?m_request.Title():m_request.ShowName());
            auto line=m_request.EpisodeLabel();if(!line.empty()&&!m_request.SourceTagLine().empty())line=line+L" \x00B7 ";line=line+m_request.SourceTagLine();winrt::get_self<PlayerOsd>(overlay)->SourceLabel(line);
        }
        UpdateOverlayLayout();
        try{viewModel->Activate();InitializePlaybackAsync();}
        catch(...){ShowMediaPrompt(L"The player could not start this source.");}
    }

    void PlayerPage::OnUnloaded(winrt::Windows::Foundation::IInspectable const&,Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_loaded=false;
        if(m_viewModel&&m_presentationChangedToken.value!=0){m_viewModel.PropertyChanged(m_presentationChangedToken);m_presentationChangedToken={};}
        OverlayPopup().IsOpen(false);
        if(m_session)m_session->Stop();App::Services().Subtitles->Stop();
        if(m_viewModel){auto vm=winrt::get_self<PlayerViewModel>(m_viewModel);vm->SetCloseRequestedHandler({});vm->SetAddonSubtitleHandler({});vm->Deactivate();}
        auto const videoHost=FindName(L"VideoHost").as<winrt::HaloDesktop::VideoHostControl>();winrt::get_self<VideoHostControl>(videoHost)->DestroyHostWindow();
        m_session.reset();
    }

    winrt::fire_and_forget PlayerPage::InitializePlaybackAsync()
    {
        auto lifetime=get_strong();
        if(!m_request){ShowMediaPrompt(L"This source is no longer available. Return to Sources and choose it again.");co_return;}
        auto&services=App::Services();m_session=std::make_shared<::HaloDesktop::Playback::PlaybackSessionController>(services.Playback,services.WatchState);
        m_session->SetErrorHandler([weak=get_weak()](){if(auto self=weak.get())self->ShowMediaPrompt(L"Playback failed for this source. Return to Sources and choose another one.");});
        try{co_await services.Subtitles->PrepareAsync(m_request);}catch(...){}
        try{co_await m_session->StartAsync(m_request);}
        catch(...){if(m_loaded)ShowMediaPrompt(L"The player could not start this source. Return to Sources and try another one.");}
    }

    winrt::fire_and_forget PlayerPage::BeginClose()
    {
        auto lifetime=get_strong();if(m_closing)co_return;co_await PrepareForWindowCloseAsync();
        App::Services().Navigation->CloseOverlay();
    }

    winrt::Windows::Foundation::IAsyncAction PlayerPage::PrepareForWindowCloseAsync()
    {
        auto lifetime=get_strong();if(m_closing)co_return;m_closing=true;
        if(m_session)co_await m_session->CloseAsync();
        if(m_viewModel)winrt::get_self<PlayerViewModel>(m_viewModel)->Deactivate();
        auto const videoHost=FindName(L"VideoHost").as<winrt::HaloDesktop::VideoHostControl>();winrt::get_self<VideoHostControl>(videoHost)->DestroyHostWindow();
    }

    void PlayerPage::ShowMediaPrompt(winrt::hstring const&message){FindName(L"MediaPromptMessage").as<Microsoft::UI::Xaml::Controls::TextBlock>().Text(message);FindName(L"MediaPrompt").as<Microsoft::UI::Xaml::Controls::Border>().Visibility(Microsoft::UI::Xaml::Visibility::Visible);}
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

    void PlayerPage::UpdateOverlayLayout()
    {
        auto const root=PlayerRoot();if(root.ActualWidth()<=0.0||root.ActualHeight()<=0.0){OverlayPopup().IsOpen(false);return;}
        OverlayHost().Width(root.ActualWidth());OverlayHost().Height(root.ActualHeight());if(m_loaded&&!OverlayPopup().IsOpen()){OverlayPopup().IsOpen(true);OverlayHost().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);}
    }
    void PlayerPage::RefreshOverlayAfterPresentationChange()
    {
        if(!m_loaded)return;OverlayPopup().IsOpen(false);auto const enqueued=DispatcherQueue().TryEnqueue(Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,[weak=get_weak()](){if(auto self=weak.get();self&&self->m_loaded)self->UpdateOverlayLayout();});if(!enqueued)UpdateOverlayLayout();
    }
}
