#pragma once

#include "VideoHostControl.g.h"

#include "Playback/IPlaybackEngine.h"

#include <cstdint>
#include <dxgi1_3.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct VideoHostControl : VideoHostControlT<VideoHostControl>
    {
        VideoHostControl();
        ~VideoHostControl();

        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSizeChanged(winrt::Windows::Foundation::IInspectable const&,
                           Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnCompositionScaleChanged(Microsoft::UI::Xaml::Controls::SwapChainPanel const&,
                                       winrt::Windows::Foundation::IInspectable const&);

        // Attaches the engine to this panel. Safe to call more than once.
        void EnsureSurface();
        // Stops playback and releases the swapchain. Safe to call more than once.
        void ReleaseSurface() noexcept;

    private:
        // Not const: the generated VideoPanel() accessor is non-const.
        [[nodiscard]] ::HaloDesktop::Playback::VideoSurfaceSize CurrentSurfaceSize();
        void UpdateSurfaceSize() noexcept;
        void ApplySwapChain(std::uintptr_t address) noexcept;
        void ApplyScaleTransform();

        winrt::com_ptr<IDXGISwapChain2> m_swapChain;
        bool m_surfaceAttached{};
    };
} // namespace winrt::HaloDesktop::implementation

namespace winrt::HaloDesktop::factory_implementation
{
    struct VideoHostControl : VideoHostControlT<VideoHostControl, implementation::VideoHostControl>
    {
    };
} // namespace winrt::HaloDesktop::factory_implementation
