#pragma once

#include "VideoHostControl.g.h"

#include <cstdint>
#include <optional>
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
        struct HostBounds final
        {
            int X{};
            int Y{};
            int Width{};
            int Height{};

            bool operator==(HostBounds const&) const = default;
        };

        void UpdateBounds() noexcept;
        void TryUpdateBounds(bool allowRetry) noexcept;
        void QueueBoundsRetry() noexcept;

        std::uintptr_t m_hostWindow{};
        std::optional<HostBounds> m_lastBounds;
        std::uint64_t m_hostWindowGeneration{};
        bool m_boundsRetryQueued{};
        Microsoft::UI::Xaml::XamlRoot::Changed_revoker m_xamlRootChangedRevoker{};
    };
} // namespace winrt::HaloDesktop::implementation

namespace winrt::HaloDesktop::factory_implementation
{
    struct VideoHostControl : VideoHostControlT<VideoHostControl, implementation::VideoHostControl>
    {
    };
} // namespace winrt::HaloDesktop::factory_implementation
