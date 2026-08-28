#pragma once

#include "QualityBadge.g.h"

namespace winrt::HaloDesktop::implementation
{
    // The picture badge shared by the player overlay and the sources sheet: a short
    // tier token knocked out of a plate, with an optional qualifier beside it.
    struct QualityBadge : QualityBadgeT<QualityBadge>
    {
        QualityBadge();

        [[nodiscard]] winrt::hstring Tier() const;
        void Tier(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring Detail() const;
        void Detail(winrt::hstring const& value);
        [[nodiscard]] winrt::HaloDesktop::QualityBadgeTone Tone() const noexcept;
        void Tone(winrt::HaloDesktop::QualityBadgeTone value);
        [[nodiscard]] double LabelSize() const noexcept;
        void LabelSize(double value);

        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void ApplyTone();

        winrt::hstring m_tier;
        winrt::hstring m_detail;
        winrt::HaloDesktop::QualityBadgeTone m_tone{ winrt::HaloDesktop::QualityBadgeTone::Gold };
        double m_labelSize{ 10.5 };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct QualityBadge : QualityBadgeT<QualityBadge, implementation::QualityBadge> {};
}
