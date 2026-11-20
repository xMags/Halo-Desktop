#include "pch.h"
#include "Views/DetailBackdropPresenter.h"

#include "Controls/ThemeColorLookup.h"

#include <chrono>
#include <span>
#include <stdexcept>
#include <utility>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace
{
    using winrt::Microsoft::UI::Xaml::Hosting::ElementCompositionPreview;

    // Decoded no larger than this, which still covers the widest page on a
    // dense display without holding an 8K original in memory.
    constexpr winrt::Windows::Foundation::Size ArtDecodeLimit{ 2560.0F, 1440.0F };
    // `dh-settle`: 1.6s, cubic-bezier(0.2, 0.8, 0.2, 1), from scale 1.06 and opacity 0.4.
    constexpr std::chrono::milliseconds SettleDuration{ 1600 };
    constexpr float SettleScale = 1.06F;
    constexpr float SettleOpacity = 0.4F;

    // `.detail-bg-scrim`: the page's ground, heaviest behind the text column,
    // lifting toward the art's open side, at 0%, 34%, 62% and 100%. The light
    // theme's dark text needs a heavier wash over the same art than the dark
    // theme's light text does.
    struct ScrimStop final
    {
        float Offset;
        std::uint8_t Alpha;
    };
    constexpr ScrimStop DarkScrimStops[]{
        { 0.0F, 0xDB },
        { 0.34F, 0x85 },
        { 0.62F, 0x1A },
        { 1.0F, 0x52 },
    };
    constexpr ScrimStop LightScrimStops[]{
        { 0.0F, 0xF0 },
        { 0.34F, 0xC2 },
        { 0.62F, 0x2E },
        { 1.0F, 0x47 },
    };
    // The dark theme's ground, if the token cannot be read.
    constexpr winrt::Windows::UI::Color FallbackGround{ 0xFF, 0x0B, 0x0B, 0x0D };

    bool AnimationsEnabled()
    {
        try
        {
            return winrt::Windows::UI::ViewManagement::UISettings{}.AnimationsEnabled();
        }
        catch (winrt::hresult_error const&)
        {
            return false;
        }
    }
}

namespace HaloDesktop::Views
{
    DetailBackdropPresenter::DetailBackdropPresenter(winrt::Microsoft::UI::Xaml::FrameworkElement host)
        : m_host(std::move(host))
    {
        if (!m_host)
        {
            throw std::invalid_argument{ "The title page's art needs a host." };
        }
        try
        {
            Build();
        }
        catch (winrt::hresult_error const&)
        {
            Disable();
        }
    }

    DetailBackdropPresenter::~DetailBackdropPresenter()
    {
        Release();
        try
        {
            if (!m_disabled) ElementCompositionPreview::SetElementChildVisual(m_host, nullptr);
        }
        catch (...)
        {
        }
    }

    void DetailBackdropPresenter::Build()
    {
        m_compositor = ElementCompositionPreview::GetElementVisual(m_host).Compositor();
        m_root = m_compositor.CreateContainerVisual();

        // `mask-image: linear-gradient(180deg, #000 66%, transparent 100%)`.
        m_mask = m_compositor.CreateLinearGradientBrush();
        m_mask.StartPoint({ 0.0F, 0.0F });
        m_mask.EndPoint({ 0.0F, 1.0F });
        winrt::Windows::UI::Color const opaque{ 0xFF, 0x00, 0x00, 0x00 };
        winrt::Windows::UI::Color const clear{ 0x00, 0x00, 0x00, 0x00 };
        m_mask.ColorStops().Append(m_compositor.CreateColorGradientStop(0.0F, opaque));
        m_mask.ColorStops().Append(m_compositor.CreateColorGradientStop(0.66F, opaque));
        m_mask.ColorStops().Append(m_compositor.CreateColorGradientStop(1.0F, clear));

        m_art = m_compositor.CreateSpriteVisual();
        m_art.Opacity(0.0F);

        m_scrimGradient = m_compositor.CreateLinearGradientBrush();
        m_scrimGradient.StartPoint({ 0.0F, 0.0F });
        m_scrimGradient.EndPoint({ 1.0F, 0.0F });
        ApplyTheme();
        auto const scrimBrush = m_compositor.CreateMaskBrush();
        scrimBrush.Source(m_scrimGradient);
        scrimBrush.Mask(m_mask);
        m_scrim = m_compositor.CreateSpriteVisual();
        m_scrim.Brush(scrimBrush);

        m_root.Children().InsertAtTop(m_art);
        m_root.Children().InsertAtTop(m_scrim);
        ElementCompositionPreview::SetElementChildVisual(m_host, m_root);
        m_themeChanged = m_host.ActualThemeChanged(winrt::auto_revoke, [this](auto&&, auto&&)
        {
            if (m_disabled) return;
            try
            {
                ApplyTheme();
            }
            catch (winrt::hresult_error const&)
            {
                Disable();
            }
        });
    }

    void DetailBackdropPresenter::ApplyTheme()
    {
        auto const theme = m_host.ActualTheme();
        auto const light = theme == winrt::Microsoft::UI::Xaml::ElementTheme::Light;
        auto const ground = ::HaloDesktop::Controls::ThemeColor(theme, L"HaloDetailGroundColor").value_or(FallbackGround);
        auto const stops = m_scrimGradient.ColorStops();
        stops.Clear();
        for (auto const& stop : light ? std::span<ScrimStop const>{ LightScrimStops } : std::span<ScrimStop const>{ DarkScrimStops })
        {
            stops.Append(m_compositor.CreateColorGradientStop(stop.Offset, { stop.Alpha, ground.R, ground.G, ground.B }));
        }
    }

    void DetailBackdropPresenter::SetArt(std::wstring_view url)
    {
        if (m_disabled || url == m_url) return;
        m_url = std::wstring{ url };
        try
        {
            m_loaded.revoke();
            if (m_surface)
            {
                m_surface.Close();
                m_surface = nullptr;
            }
            m_art.StopAnimation(L"Opacity");
            m_art.StopAnimation(L"Scale");
            m_art.Opacity(0.0F);
            m_art.Scale({ 1.0F, 1.0F, 1.0F });
            m_art.Brush(nullptr);
            if (m_url.empty()) return;

            // Loaded and decoded off the UI thread.
            m_surface = winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface::StartLoadFromUri(
                winrt::Windows::Foundation::Uri{ m_url }, ArtDecodeLimit);
            auto const art = m_compositor.CreateSurfaceBrush(m_surface);
            art.Stretch(winrt::Microsoft::UI::Composition::CompositionStretch::UniformToFill);
            // `center 20%`.
            art.HorizontalAlignmentRatio(0.5F);
            art.VerticalAlignmentRatio(0.2F);
            auto const masked = m_compositor.CreateMaskBrush();
            masked.Source(art);
            masked.Mask(m_mask);
            m_art.Brush(masked);
            m_loaded = m_surface.LoadCompleted(winrt::auto_revoke, [this](
                winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface const& surface,
                winrt::Microsoft::UI::Xaml::Media::LoadedImageSourceLoadCompletedEventArgs const& args)
            {
                OnLoaded(surface, args.Status() == winrt::Microsoft::UI::Xaml::Media::LoadedImageSourceLoadStatus::Success);
            });
        }
        catch (winrt::hresult_error const&)
        {
            // A URL the loader refuses leaves the blurred art on its own, as a
            // picture that fails to load does.
            try
            {
                m_art.Brush(nullptr);
            }
            catch (winrt::hresult_error const&)
            {
                Disable();
            }
        }
    }

    void DetailBackdropPresenter::Resize(double width, double height)
    {
        if (m_disabled) return;
        m_width = static_cast<float>(width > 0.0 ? width : 0.0);
        m_height = static_cast<float>(height > 0.0 ? height : 0.0);
        try
        {
            winrt::Windows::Foundation::Numerics::float2 const size{ m_width, m_height };
            m_root.Size(size);
            m_art.Size(size);
            m_scrim.Size(size);
            // The settle scales about the art's centre, as a CSS transform does.
            m_art.CenterPoint({ m_width / 2.0F, m_height / 2.0F, 0.0F });
        }
        catch (winrt::hresult_error const&)
        {
            Disable();
        }
    }

    void DetailBackdropPresenter::OnLoaded(winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface const& surface, bool succeeded)
    {
        if (m_disabled || surface != m_surface) return;
        try
        {
            if (!succeeded)
            {
                m_art.Brush(nullptr);
                return;
            }
            if (!AnimationsEnabled())
            {
                m_art.Opacity(1.0F);
                return;
            }
            auto const easing = m_compositor.CreateCubicBezierEasingFunction({ 0.2F, 0.8F }, { 0.2F, 1.0F });
            auto const duration = std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(SettleDuration);
            auto const fade = m_compositor.CreateScalarKeyFrameAnimation();
            fade.InsertKeyFrame(0.0F, SettleOpacity);
            fade.InsertKeyFrame(1.0F, 1.0F, easing);
            fade.Duration(duration);
            auto const settle = m_compositor.CreateVector3KeyFrameAnimation();
            settle.InsertKeyFrame(0.0F, { SettleScale, SettleScale, 1.0F });
            settle.InsertKeyFrame(1.0F, { 1.0F, 1.0F, 1.0F }, easing);
            settle.Duration(duration);
            m_art.Opacity(1.0F);
            m_art.Scale({ 1.0F, 1.0F, 1.0F });
            m_art.StartAnimation(L"Opacity", fade);
            m_art.StartAnimation(L"Scale", settle);
        }
        catch (winrt::hresult_error const&)
        {
            try
            {
                m_art.Opacity(1.0F);
            }
            catch (winrt::hresult_error const&)
            {
                Disable();
            }
        }
    }

    void DetailBackdropPresenter::Release() noexcept
    {
        try
        {
            m_themeChanged.revoke();
            m_loaded.revoke();
            if (m_surface)
            {
                m_surface.Close();
                m_surface = nullptr;
            }
        }
        catch (...)
        {
        }
    }

    // Composition refused once: the page goes on without the sharp art.
    void DetailBackdropPresenter::Disable() noexcept
    {
        m_disabled = true;
        Release();
        try
        {
            if (m_host) ElementCompositionPreview::SetElementChildVisual(m_host, nullptr);
        }
        catch (...)
        {
        }
        m_art = nullptr;
        m_scrim = nullptr;
        m_root = nullptr;
    }
}
