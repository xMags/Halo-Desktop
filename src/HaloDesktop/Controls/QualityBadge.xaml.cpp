#include "pch.h"
#include "Controls/QualityBadge.xaml.h"
#if __has_include("QualityBadge.g.cpp")
#include "QualityBadge.g.cpp"
#endif

namespace winrt::HaloDesktop::implementation
{
    QualityBadge::QualityBadge()
    {
        InitializeComponent();
    }

    winrt::hstring QualityBadge::Tier() const { return m_tier; }
    void QualityBadge::Tier(winrt::hstring const& value)
    {
        m_tier = value;
        if (auto const text = FindName(L"TierText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>())
        {
            text.Text(value);
        }
    }

    winrt::hstring QualityBadge::Detail() const { return m_detail; }
    void QualityBadge::Detail(winrt::hstring const& value)
    {
        m_detail = value;
        auto const text = FindName(L"DetailText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>();
        if (!text)
        {
            return;
        }
        text.Text(value);
        text.Visibility(value.empty() ? Microsoft::UI::Xaml::Visibility::Collapsed
                                      : Microsoft::UI::Xaml::Visibility::Visible);
    }

    winrt::HaloDesktop::QualityBadgeTone QualityBadge::Tone() const noexcept { return m_tone; }
    void QualityBadge::Tone(winrt::HaloDesktop::QualityBadgeTone value)
    {
        m_tone = value;
        ApplyTone();
    }

    double QualityBadge::LabelSize() const noexcept { return m_labelSize; }
    void QualityBadge::LabelSize(double value)
    {
        if (value <= 0.0)
        {
            return;
        }
        m_labelSize = value;
        if (auto const tier = FindName(L"TierText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>())
        {
            tier.FontSize(value);
        }
        if (auto const detail = FindName(L"DetailText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>())
        {
            detail.FontSize(value);
        }
    }

    // A recycled row is re-bound before it is loaded, and a visual state applied to
    // an unloaded control does not always stick, so the tone is asserted again here.
    void QualityBadge::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ApplyTone();
    }

    void QualityBadge::ApplyTone()
    {
        auto const state = m_tone == winrt::HaloDesktop::QualityBadgeTone::Muted ? L"Muted" : L"Gold";
        Microsoft::UI::Xaml::VisualStateManager::GoToState(*this, state, false);
    }
}
