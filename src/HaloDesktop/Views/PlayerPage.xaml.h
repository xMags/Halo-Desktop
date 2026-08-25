#pragma once

#include "PlayerPage.g.h"
#include "Playback/UpNextResolver.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <winrt/Windows.Foundation.h>

namespace HaloDesktop::Playback { class PlaybackSessionController; }

namespace winrt::HaloDesktop::implementation
{
    struct PlayerPage : PlayerPageT<PlayerPage>
    {
        PlayerPage();
        [[nodiscard]] winrt::HaloDesktop::PlayerViewModel ViewModel() const;
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction PrepareForWindowCloseAsync();
        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args);
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPlayerSizeChanged(winrt::Windows::Foundation::IInspectable const&,
                                 Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnCloseErrorClick(winrt::Windows::Foundation::IInspectable const&,Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSpaceInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                            Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnLeftInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                           Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnRightInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                            Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnUpInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                         Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnDownInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                           Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnFullscreenInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                                 Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnEscapeInvoked(Microsoft::UI::Xaml::Input::KeyboardAccelerator const&,
                             Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const&);
        void OnOverlayPreviewKeyDown(winrt::Windows::Foundation::IInspectable const&,
                                     Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);

    private:
        winrt::fire_and_forget InitializePlaybackAsync();
        winrt::Windows::Foundation::IAsyncAction StartRequestAsync(winrt::HaloDesktop::PlaybackRequest request);
        winrt::fire_and_forget RefreshPlaybackSettingsAsync(std::uint64_t generation);
        winrt::fire_and_forget PrefetchUpNextAsync(winrt::HaloDesktop::PlaybackRequest request, std::uint64_t generation);
        winrt::fire_and_forget AdvanceUpNextAsync();
        winrt::fire_and_forget BeginClose();
        void ShowMediaPrompt(winrt::hstring const& message);
        void ShowSubtitleError();
        void CloseOverlayPopup(bool detachChild) noexcept;
        void UpdateOverlayLayout();
        void RefreshOverlayAfterPresentationChange();
        void CompleteKeyboardAction(Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);

        winrt::HaloDesktop::PlayerViewModel m_viewModel{ nullptr };
        winrt::HaloDesktop::PlaybackRequest m_request{ nullptr };
        std::shared_ptr<::HaloDesktop::Playback::PlaybackSessionController> m_session;
        std::optional<::HaloDesktop::Playback::UpNextResult> m_upNext;
        winrt::event_token m_presentationChangedToken{};
        std::uint64_t m_playbackGeneration{};
        bool m_loaded{};
        bool m_closing{};
        bool m_advancing{};
    };
} // namespace winrt::HaloDesktop::implementation

namespace winrt::HaloDesktop::factory_implementation
{
    struct PlayerPage : PlayerPageT<PlayerPage, implementation::PlayerPage>
    {
    };
} // namespace winrt::HaloDesktop::factory_implementation
