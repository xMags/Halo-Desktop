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

        [[nodiscard]] bool FillsColumn() const noexcept;
        void FillsColumn(bool value);
        [[nodiscard]] bool ShowKindBadge() const noexcept;
        void ShowKindBadge(bool value);

        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void ApplyLayoutMetrics();
        void ApplyColumnWidth(double width);

        std::uint64_t m_metricsToken{};
        bool m_fillsColumn{};
        bool m_showKindBadge{};
        Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker m_sizeChanged;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct PosterSkeleton : PosterSkeletonT<PosterSkeleton, implementation::PosterSkeleton>
    {
    };
}
