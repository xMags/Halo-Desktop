#pragma once

#include "VideoHostControl.g.h"

#include <cstdint>
#include <winrt/Microsoft.UI.Xaml.h>
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

        void EnsureHostWindow();
        void DestroyHostWindow() noexcept;
        [[nodiscard]] std::uintptr_t HostWindowHandle() const noexcept;

    private:
        void UpdateBounds();

        std::uintptr_t m_hostWindow{};
        Microsoft::UI::Xaml::XamlRoot::Changed_revoker m_xamlRootChangedRevoker{};
    };
} // namespace winrt::HaloDesktop::implementation

namespace winrt::HaloDesktop::factory_implementation
{
    struct VideoHostControl : VideoHostControlT<VideoHostControl, implementation::VideoHostControl>
    {
    };
} // namespace winrt::HaloDesktop::factory_implementation
