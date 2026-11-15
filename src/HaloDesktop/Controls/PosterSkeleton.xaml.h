#pragma once

#include "PosterSkeleton.g.h"

#include <cstdint>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct PosterSkeleton : PosterSkeletonT<PosterSkeleton>
    {
        PosterSkeleton();

        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void ApplyLayoutMetrics();

        std::uint64_t m_metricsToken{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct PosterSkeleton : PosterSkeletonT<PosterSkeleton, implementation::PosterSkeleton>
    {
    };
}
