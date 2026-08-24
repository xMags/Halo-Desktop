#include "pch.h"
#include "Views/PlayerPage.xaml.h"
#if __has_include("PlayerPage.g.cpp")
#include "PlayerPage.g.cpp"
#endif

#include "Controls/PlayerOsd.xaml.h"
#include "Controls/VideoHostControl.xaml.h"
#include "Playback/IPlaybackEngine.h"
#include "Playback/LocalPlaybackQueue.h"
#include "App.xaml.h"
#include "Shell/WindowPresentationService.h"
#include "ViewModels/PlayerViewModel.h"

#include <filesystem>
#include <shobjidl_core.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <wil/cppwinrt_helpers.h>
#include <wil/resource.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.h>

namespace
{
    constexpr wchar_t LastPlaybackFileKey[] = L"HaloDesktop.Playback.LastFile";
}

namespace winrt::HaloDesktop::implementation
{
    PlayerPage::PlayerPage() = default;
    winrt::HaloDesktop::PlayerViewModel PlayerPage::ViewModel() const
    {
        return m_viewModel;
    }
    void PlayerPage::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                              [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_loaded = true;
        auto const videoHost = FindName(L"VideoHost").as<winrt::HaloDesktop::VideoHostControl>();
        winrt::get_self<VideoHostControl>(videoHost)->EnsureHostWindow();
        m_viewModel = FindName(L"PlayerOverlay").as<winrt::HaloDesktop::PlayerOsd>().ViewModel();
        winrt::get_self<PlayerViewModel>(m_viewModel)->SetPlayNextHandler([weak = get_weak()] {
            if (auto const self = weak.get())
            {
                self->TryPlayNext();
            }
        });
        m_presentationChangedToken = m_viewModel.PropertyChanged(
            [weak = get_weak()]([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                                Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args) {
                if (args.PropertyName() != L"IsFullscreen")
                {
                    return;
                }
                if (auto const self = weak.get())
                {
                    self->RefreshOverlayAfterPresentationChange();
                }
            });
        UpdateOverlayLayout();
        OpenRememberedFileOrPrompt();
        winrt::get_self<PlayerViewModel>(m_viewModel)->Activate();
    }
    void PlayerPage::OnUnloaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_loaded = false;
        ++m_upNextRequestId;
        if (m_viewModel && m_presentationChangedToken.value != 0)
        {
            m_viewModel.PropertyChanged(m_presentationChangedToken);
            m_presentationChangedToken = {};
        }
        OverlayPopup().IsOpen(false);
        if (m_viewModel)
        {
            auto const viewModel = winrt::get_self<PlayerViewModel>(m_viewModel);
            viewModel->SetPlayNextHandler({});
            viewModel->Deactivate();
        }
        m_nextSource.reset();
        auto const videoHost = FindName(L"VideoHost").as<winrt::HaloDesktop::VideoHostControl>();
        winrt::get_self<VideoHostControl>(videoHost)->DestroyHostWindow();
    }
    void PlayerPage::OnPlayerSizeChanged([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                                         [[maybe_unused]] Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        UpdateOverlayLayout();
    }
    void PlayerPage::OnOpenFileClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                                     [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        OpenFilePicker();
    }
    void PlayerPage::OnSpaceInvoked([[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                    Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        m_viewModel.TogglePause();
        CompleteKeyboardAction(args);
    }
    void PlayerPage::OnLeftInvoked([[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                   Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        m_viewModel.SeekRelative(-10.0);
        CompleteKeyboardAction(args);
    }
    void PlayerPage::OnRightInvoked([[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                    Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        m_viewModel.SeekRelative(10.0);
        CompleteKeyboardAction(args);
    }
    void PlayerPage::OnUpInvoked([[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                 Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        m_viewModel.ChangeVolume(5.0);
        CompleteKeyboardAction(args);
    }
    void PlayerPage::OnDownInvoked([[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                   Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        m_viewModel.ChangeVolume(-5.0);
        CompleteKeyboardAction(args);
    }
    void PlayerPage::OnFullscreenInvoked([[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                         Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        m_viewModel.ToggleFullscreen();
        CompleteKeyboardAction(args);
    }
    void PlayerPage::OnEscapeInvoked([[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                     Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        m_viewModel.HandleEscape();
        CompleteKeyboardAction(args);
    }
    void PlayerPage::OnOverlayPreviewKeyDown(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        // The overlay lives in a windowed popup, which is its own focus root and swallows
        // Escape before accelerators run. Tunnelling KeyDown sees every transport key first.
        using winrt::Windows::System::VirtualKey;
        if (!m_viewModel)
        {
            return;
        }

        switch (args.Key())
        {
        case VirtualKey::Space: m_viewModel.TogglePause(); break;
        case VirtualKey::Left: m_viewModel.SeekRelative(-10.0); break;
        case VirtualKey::Right: m_viewModel.SeekRelative(10.0); break;
        case VirtualKey::Up: m_viewModel.ChangeVolume(5.0); break;
        case VirtualKey::Down: m_viewModel.ChangeVolume(-5.0); break;
        case VirtualKey::F: m_viewModel.ToggleFullscreen(); break;
        case VirtualKey::Escape: m_viewModel.HandleEscape(); break;
        default: return;
        }

        m_viewModel.NotifyUserActivity();
        args.Handled(true);
    }

    void PlayerPage::CompleteKeyboardAction(Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        if (m_viewModel)
        {
            m_viewModel.NotifyUserActivity();
        }
        args.Handled(true);
    }

    void PlayerPage::OpenRememberedFileOrPrompt()
    {
        auto const values = winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values();
        auto const stored = values.TryLookup(LastPlaybackFileKey);
        if (stored)
        {
            auto const path = winrt::unbox_value_or<winrt::hstring>(stored, {});
            std::error_code error;
            if (!path.empty() && std::filesystem::is_regular_file(std::filesystem::path(path.c_str()), error) && !error)
            {
                try
                {
                    OpenSource(path);
                    return;
                }
                catch (...)
                {
                }
            }
            static_cast<void>(values.Remove(LastPlaybackFileKey));
        }

        ShowMediaPrompt(L"Choose a local video file. Halo will remember it for the next Play action.");
        OpenFilePicker();
    }

    winrt::fire_and_forget PlayerPage::OpenFilePicker()
    {
        auto const lifetime = get_strong();
        if (m_pickerOpen)
        {
            co_return;
        }

        m_pickerOpen = true;
        auto const resetPickerState = wil::scope_exit([this] { m_pickerOpen = false; });

        try
        {
            auto const picker = winrt::Windows::Storage::Pickers::FileOpenPicker{};
            picker.ViewMode(winrt::Windows::Storage::Pickers::PickerViewMode::Thumbnail);
            picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::VideosLibrary);
            for (auto const extension : ::HaloDesktop::Playback::LocalPlaybackQueue::SupportedExtensions())
            {
                picker.FileTypeFilter().Append(winrt::hstring(extension));
            }

            // IInitializeWithWindow is a Win32 boundary and requires the exact
            // HWND value retained by the window presentation service.
            auto const windowHandle = reinterpret_cast<HWND>(App::Services().WindowPresentation->WindowHandle());
            winrt::check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(windowHandle));
            auto const file = co_await picker.PickSingleFileAsync();
            if (!m_loaded)
            {
                co_return;
            }
            if (!file)
            {
                ShowMediaPrompt(L"No file was selected. Choose a local video when you are ready.");
                co_return;
            }

            OpenSource(file.Path());
        }
        catch (...)
        {
            if (m_loaded)
            {
                ShowMediaPrompt(L"Halo could not open that local video. Choose another file and try again.");
            }
        }
    }

    void PlayerPage::OpenSource(winrt::hstring const& path)
    {
        auto const source = std::filesystem::path(path.c_str());
        App::Services().Playback->Open(source.wstring());
        winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values().Insert(LastPlaybackFileKey,
                                                                                            winrt::box_value(path));

        auto const fileName = source.filename().wstring();
        auto const overlay = FindName(L"PlayerOverlay").as<winrt::HaloDesktop::PlayerOsd>();
        winrt::get_self<PlayerOsd>(overlay)->SourceLabel(winrt::hstring(L"LOCAL FILE · " + fileName));
        UpdateUpNext(source);
        FindName(L"MediaPrompt")
            .as<Microsoft::UI::Xaml::Controls::Border>()
            .Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
    }

    void PlayerPage::ShowMediaPrompt(winrt::hstring const& message)
    {
        FindName(L"MediaPromptMessage").as<Microsoft::UI::Xaml::Controls::TextBlock>().Text(message);
        FindName(L"MediaPrompt")
            .as<Microsoft::UI::Xaml::Controls::Border>()
            .Visibility(Microsoft::UI::Xaml::Visibility::Visible);
    }

    winrt::fire_and_forget PlayerPage::UpdateUpNext(std::filesystem::path currentSource)
    {
        auto const lifetime = get_strong();
        auto const requestId = ++m_upNextRequestId;
        auto const dispatcher = DispatcherQueue();
        m_nextSource.reset();
        winrt::get_self<PlayerViewModel>(m_viewModel)->SetUpNextTitle({});

        co_await winrt::resume_background();

        std::optional<std::filesystem::path> nextSource;
        try
        {
            nextSource = ::HaloDesktop::Playback::LocalPlaybackQueue::NextAfter(currentSource);
        }
        catch (...)
        {
        }

        try
        {
            co_await wil::resume_foreground(dispatcher);
        }
        catch (...)
        {
            co_return;
        }
        if (!m_loaded || requestId != m_upNextRequestId)
        {
            co_return;
        }

        m_nextSource = std::move(nextSource);
        auto const title = m_nextSource ? winrt::hstring(m_nextSource->filename().wstring()) : winrt::hstring{};
        winrt::get_self<PlayerViewModel>(m_viewModel)->SetUpNextTitle(title);
    }

    void PlayerPage::TryPlayNext()
    {
        if (!m_loaded || !m_nextSource)
        {
            return;
        }

        auto const nextSource = *m_nextSource;
        try
        {
            OpenSource(winrt::hstring(nextSource.wstring()));
        }
        catch (...)
        {
            m_nextSource.reset();
            winrt::get_self<PlayerViewModel>(m_viewModel)->SetUpNextTitle({});
            ShowMediaPrompt(L"Halo could not open the next local video. Choose another file and try again.");
        }
    }

    void PlayerPage::UpdateOverlayLayout()
    {
        // A popup never inherits its parent's size, so the overlay is measured against
        // the player surface by hand and re-measured on every resize.
        auto const root = PlayerRoot();
        if (root.ActualWidth() <= 0.0 || root.ActualHeight() <= 0.0)
        {
            OverlayPopup().IsOpen(false);
            return;
        }

        OverlayHost().Width(root.ActualWidth());
        OverlayHost().Height(root.ActualHeight());
        if (m_loaded && !OverlayPopup().IsOpen())
        {
            OverlayPopup().IsOpen(true);
            // Without focus in the popup tree the transport shortcuts have nowhere to route.
            OverlayHost().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
        }
    }

    void PlayerPage::RefreshOverlayAfterPresentationChange()
    {
        if (!m_loaded)
        {
            return;
        }

        // A native fullscreen transition can move the main HWND above the popup's
        // composition window. Reopening it after layout settles restores both its
        // z-order and its independent focus root without touching video playback.
        OverlayPopup().IsOpen(false);
        auto const enqueued = DispatcherQueue().TryEnqueue(
            Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [weak = get_weak()] {
                if (auto const self = weak.get(); self && self->m_loaded)
                {
                    self->UpdateOverlayLayout();
                }
            });
        if (!enqueued)
        {
            UpdateOverlayLayout();
        }
    }
} // namespace winrt::HaloDesktop::implementation
