#pragma once

#include "PlayerOsd.g.h"

#include <cstdint>
#include <optional>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct PlayerOsd : PlayerOsdT<PlayerOsd>
    {
        PlayerOsd();
        [[nodiscard]] winrt::HaloDesktop::PlayerViewModel ViewModel() const;
        void TitleLabel(winrt::hstring const& value);
        void SourceLabel(winrt::hstring const& value);
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnOsdPointerMoved(winrt::Windows::Foundation::IInspectable const&,
                               Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnOsdWakePointerPressed(winrt::Windows::Foundation::IInspectable const&,
                                     Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnBackClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnTogglePauseClick(winrt::Windows::Foundation::IInspectable const&,
                                Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnReplayClick(winrt::Windows::Foundation::IInspectable const&,
                           Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnForwardClick(winrt::Windows::Foundation::IInspectable const&,
                            Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSeekPointerPressed(winrt::Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnSeekPointerMoved(winrt::Windows::Foundation::IInspectable const&,
                                Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnSeekPointerReleased(winrt::Windows::Foundation::IInspectable const&,
                                   Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnSeekPointerTerminated(winrt::Windows::Foundation::IInspectable const&,
                                     Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnFullscreenClick(winrt::Windows::Foundation::IInspectable const&,
                               Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAudioPanelClick(winrt::Windows::Foundation::IInspectable const&,
                               Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSubtitlesPanelClick(winrt::Windows::Foundation::IInspectable const&,
                                   Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSpeedPanelClick(winrt::Windows::Foundation::IInspectable const&,
                               Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUpNextClick(winrt::Windows::Foundation::IInspectable const&,
                           Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnClosePanelClick(winrt::Windows::Foundation::IInspectable const&,
                               Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAudioTabClick(winrt::Windows::Foundation::IInspectable const&,
                             Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSubtitlesTabClick(winrt::Windows::Foundation::IInspectable const&,
                                 Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSpeedTabClick(winrt::Windows::Foundation::IInspectable const&,
                             Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAudioTrackClick(winrt::Windows::Foundation::IInspectable const&,
                               Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSubtitlesOffClick(winrt::Windows::Foundation::IInspectable const&,
                                 Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSubtitleTrackClick(winrt::Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAddonSubtitleClick(winrt::Windows::Foundation::IInspectable const&,Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSubtitleDelayDownClick(winrt::Windows::Foundation::IInspectable const&,
                                      Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSubtitleDelayUpClick(winrt::Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAudioDelayDownClick(winrt::Windows::Foundation::IInspectable const&,
                                   Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAudioDelayUpClick(winrt::Windows::Foundation::IInspectable const&,
                                 Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSpeedHalfClick(winrt::Windows::Foundation::IInspectable const&,
                              Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSpeedThreeQuarterClick(winrt::Windows::Foundation::IInspectable const&,
                                      Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSpeedNormalClick(winrt::Windows::Foundation::IInspectable const&,
                                Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSpeedOneQuarterClick(winrt::Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSpeedOneHalfClick(winrt::Windows::Foundation::IInspectable const&,
                                 Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSpeedDoubleClick(winrt::Windows::Foundation::IInspectable const&,
                                Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCancelUpNextClick(winrt::Windows::Foundation::IInspectable const&,
                                 Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPlayNextClick(winrt::Windows::Foundation::IInspectable const&,
                             Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        winrt::HaloDesktop::PlayerViewModel m_viewModel{ nullptr };
        std::optional<winrt::Windows::Foundation::Point> m_lastPointerPosition;
        std::uint32_t m_lastPointerId{};
        bool m_seekHandlersRegistered{};
        bool m_seekPointerActive{};
    };
} // namespace winrt::HaloDesktop::implementation

namespace winrt::HaloDesktop::factory_implementation
{
    struct PlayerOsd : PlayerOsdT<PlayerOsd, implementation::PlayerOsd>
    {
    };
} // namespace winrt::HaloDesktop::factory_implementation
