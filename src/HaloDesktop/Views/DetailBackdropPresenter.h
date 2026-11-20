#pragma once

#include <string>
#include <string_view>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Views
{
    // The title page's sharp art (`.detail-bg-art`) and the scrim over it
    // (`.detail-bg-scrim`), drawn the way the reference draws them: the art at
    // `center 20% / cover`, both faded out over the bottom third by the same
    // mask, the art settling in over 1.6s (scale 1.06 to 1, opacity 0.4 to 1).
    // XAML has no opacity mask for an image, so both are composition visuals
    // under one host element that is used for nothing else.
    //
    // UI-thread-only, owned by the page. Decorative: every composition call is
    // guarded, and a refusal leaves the page on its blurred art instead. The
    // visuals are removed from the host when the presenter goes.
    class DetailBackdropPresenter final
    {
    public:
        explicit DetailBackdropPresenter(winrt::Microsoft::UI::Xaml::FrameworkElement host);
        ~DetailBackdropPresenter();

        DetailBackdropPresenter(DetailBackdropPresenter const&) = delete;
        DetailBackdropPresenter& operator=(DetailBackdropPresenter const&) = delete;
        DetailBackdropPresenter(DetailBackdropPresenter&&) = delete;
        DetailBackdropPresenter& operator=(DetailBackdropPresenter&&) = delete;

        // Shows `url`'s art, settling it in once it has loaded. The same URL again
        // is a no-op.
        void SetArt(std::wstring_view url);
        void Resize(double width, double height);

    private:
        void Build();
        // The scrim in the host's theme: its ground colour and its stops.
        void ApplyTheme();
        void OnLoaded(winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface const& surface, bool succeeded);
        void Release() noexcept;
        void Disable() noexcept;

        winrt::Microsoft::UI::Xaml::FrameworkElement m_host{ nullptr };
        winrt::Microsoft::UI::Composition::Compositor m_compositor{ nullptr };
        winrt::Microsoft::UI::Composition::ContainerVisual m_root{ nullptr };
        winrt::Microsoft::UI::Composition::SpriteVisual m_art{ nullptr };
        winrt::Microsoft::UI::Composition::SpriteVisual m_scrim{ nullptr };
        winrt::Microsoft::UI::Composition::CompositionLinearGradientBrush m_mask{ nullptr };
        winrt::Microsoft::UI::Composition::CompositionLinearGradientBrush m_scrimGradient{ nullptr };
        winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface m_surface{ nullptr };
        std::wstring m_url;
        float m_width{};
        float m_height{};
        bool m_disabled{};
        winrt::Microsoft::UI::Xaml::FrameworkElement::ActualThemeChanged_revoker m_themeChanged;
        // Last, so it is revoked before the surface it listens to goes.
        winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface::LoadCompleted_revoker m_loaded;
    };
}
