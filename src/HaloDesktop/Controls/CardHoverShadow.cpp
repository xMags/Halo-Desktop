#include "pch.h"
#include "Controls/CardHoverShadow.h"

#include "Controls/ThemeColorLookup.h"

#include <algorithm>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.Foundation.Numerics.h>

namespace
{
    constexpr float ShadowOffsetY = 8.0F;
    constexpr float ShadowBlur = 16.0F;
    // The negative spread is drawn by insetting the shadow's shape, which keeps
    // it off the strip's clipped sides.
    constexpr float ShadowInset = 6.0F;
}

namespace HaloDesktop::Controls
{
    void CardHoverShadow::Prepare(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& host,
        winrt::Microsoft::UI::Xaml::FrameworkElement const& themeScope)
    {
        if (!host) return;
        if (!m_visual)
        {
            auto const compositor = winrt::Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(host).Compositor();
            m_shadow = compositor.CreateDropShadow();
            m_shadow.BlurRadius(ShadowBlur);
            m_shadow.Offset({ 0.0F, ShadowOffsetY, 0.0F });
            // A brushless sprite casts the shadow of its own rectangle and
            // draws nothing else; the art's rounded corners lie outside the
            // inset rectangle, so none of them peeks out.
            m_visual = compositor.CreateSpriteVisual();
            m_visual.Shadow(m_shadow);
            m_visual.Offset({ ShadowInset, ShadowInset, 0.0F });
            winrt::Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(host, m_visual);
            // `this` is sound: the revoker is a member, so the handler goes with
            // this object, which its card owns for as long as the host lives.
            m_sizeChanged = host.SizeChanged(winrt::auto_revoke, [this](winrt::Windows::Foundation::IInspectable const& sender, auto const&)
            {
                Resize(sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>());
            });
            Resize(host);
        }
        if (auto const color = ThemeColor(themeScope, L"HaloHoverCardShadowColor"))
        {
            m_shadow.Color(*color);
        }
    }

    void CardHoverShadow::Resize(winrt::Microsoft::UI::Xaml::FrameworkElement const& host)
    {
        if (!m_visual || !host) return;
        auto const size = host.ActualSize();
        m_visual.Size({
            (std::max)(0.0F, size.x - 2.0F * ShadowInset),
            (std::max)(0.0F, size.y - 2.0F * ShadowInset) });
    }
}
