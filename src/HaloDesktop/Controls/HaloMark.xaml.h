#pragma once

#include "HaloMark.g.h"

#include "Controls/HaloMarkGeometry.h"

namespace winrt::HaloDesktop::implementation
{
    // The Halo mark drawn from HaloMarkGeometry. Size picks the ring weight: a mark at
    // or below 24 pixels gets the heavier ring and the narrower clearance.
    struct HaloMark : HaloMarkT<HaloMark>
    {
        HaloMark() = default;
        void InitializeComponent();

        [[nodiscard]] double Size() const noexcept;
        void Size(double value);

    private:
        void ApplySize();
        void ApplyWeight(::HaloDesktop::Controls::HaloMarkWeight weight);

        double m_size{ 16.0 };
        bool m_initialized{};
        bool m_weightApplied{};
        ::HaloDesktop::Controls::HaloMarkWeight m_weight{ ::HaloDesktop::Controls::HaloMarkWeight::Small };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct HaloMark : HaloMarkT<HaloMark, implementation::HaloMark> {};
}
