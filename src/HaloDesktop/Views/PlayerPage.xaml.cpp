#include "pch.h"
#include "Views/PlayerPage.xaml.h"
#if __has_include("PlayerPage.g.cpp")
#include "PlayerPage.g.cpp"
#endif

#include "Controls/PlayerOsd.xaml.h"
#include "Controls/VideoHostControl.xaml.h"
#include "Playback/IPlaybackEngine.h"
#include "App.xaml.h"
#include "Shell/WindowPresentationService.h"
#include "ViewModels/PlayerViewModel.h"

#include <filesystem>
#include <shobjidl_core.h>
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
        UpdateOverlayLayout();
        OpenRememberedFileOrPrompt();
        winrt::get_self<PlayerViewModel>(m_viewModel)->Activate();
    }
    void PlayerPage::OnUnloaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
                                [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_loaded = false;
        OverlayPopup().IsOpen(false);
        if (m_viewModel)
        {
            winrt::get_self<PlayerViewModel>(m_viewModel)->Deactivate();
        }
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
            for (auto const extension : { L".mkv", L".mp4", L".webm", L".mov", L".m4v", L".avi" })
            {
                picker.FileTypeFilter().Append(extension);
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
        App::Services().Playback->Open(std::wstring(path));
        winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values().Insert(LastPlaybackFileKey,
                                                                                            winrt::box_value(path));

        auto const fileName = std::filesystem::path(path.c_str()).filename().wstring();
        auto const overlay = FindName(L"PlayerOverlay").as<winrt::HaloDesktop::PlayerOsd>();
        winrt::get_self<PlayerOsd>(overlay)->SourceLabel(winrt::hstring(L"LOCAL FILE · " + fileName));
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
} // namespace winrt::HaloDesktop::implementation
