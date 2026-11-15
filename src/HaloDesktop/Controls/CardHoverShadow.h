#pragma once

#include <chrono>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Controls
{
    // The reference's card hover (`.poster:hover`, `.cw-card:hover` in
    // shelves.css): the card lifts 2px over 120ms while its art takes the hover
    // stroke and `box-shadow: 0 8px 16px -6px`.
    inline constexpr std::chrono::milliseconds CardHoverDuration{ 120 };
    inline constexpr float CardHoverLift = -2.0F;

    // The shadow under a hovered card's art, drawn into a host element laid
    // under the art and faded with it. Built on the first hover rather than for
    // every card realised. UI-thread-only; owned by the card it draws for, and
    // not copyable, because the host's size handler refers back to it.
    class CardHoverShadow final
    {
    public:
        CardHoverShadow() = default;
        CardHoverShadow(CardHoverShadow const&) = delete;
        CardHoverShadow& operator=(CardHoverShadow const&) = delete;
        CardHoverShadow(CardHoverShadow&&) = delete;
        CardHoverShadow& operator=(CardHoverShadow&&) = delete;

        // Builds the shadow into `host` the first time, and takes its colour
        // from the theme `themeScope` is drawn in every time, so a theme switch
        // since the last hover is honoured.
        void Prepare(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& host,
            winrt::Microsoft::UI::Xaml::FrameworkElement const& themeScope);

    private:
        void Build(winrt::Microsoft::UI::Xaml::FrameworkElement const& host);
        void Resize(winrt::Microsoft::UI::Xaml::FrameworkElement const& host);

        winrt::Microsoft::UI::Composition::SpriteVisual m_visual{ nullptr };
        winrt::Microsoft::UI::Composition::DropShadow m_shadow{ nullptr };
        winrt::Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker m_sizeChanged;
        // Set once XAML has refused the shadow, so later hovers do not ask again.
        bool m_unavailable{};
    };
}
