#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <winrt/HaloDesktop.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace HaloDesktop::Views
{
    // How long the carousel rests on one title (`theme.ts` HERO_DWELL_MS).
    inline constexpr std::chrono::milliseconds HeroDwell{ 6000 };
    // The art crossfade and the body's entrance, in step.
    inline constexpr std::chrono::milliseconds HeroFade{ 400 };
    // The carousel's length, and so the most pips the hero carries.
    inline constexpr std::size_t HeroPipCount = 5;

    // The hero's elements, all from HomePage.xaml.
    struct HomeHeroParts final
    {
        winrt::Microsoft::UI::Xaml::FrameworkElement Root{ nullptr };
        // An empty element the backdrops and the scrim are drawn into.
        winrt::Microsoft::UI::Xaml::FrameworkElement ArtHost{ nullptr };
        winrt::Microsoft::UI::Xaml::UIElement Placeholder{ nullptr };
        winrt::Microsoft::UI::Xaml::UIElement Body{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Image Logo{ nullptr };
        // Sized to the logo, beneath it, for the logo's drop shadow.
        winrt::Microsoft::UI::Xaml::FrameworkElement LogoShadowHost{ nullptr };
        winrt::Microsoft::UI::Xaml::UIElement LogoFrame{ nullptr };
        winrt::Microsoft::UI::Xaml::UIElement TitleText{ nullptr };
        winrt::Microsoft::UI::Xaml::UIElement PipStrip{ nullptr };
        std::array<winrt::Microsoft::UI::Xaml::Controls::Button, HeroPipCount> Pips{ nullptr, nullptr, nullptr, nullptr, nullptr };
        std::array<winrt::Microsoft::UI::Xaml::UIElement, HeroPipCount> PipFills{ nullptr, nullptr, nullptr, nullptr, nullptr };
        std::array<winrt::Microsoft::UI::Xaml::Media::ScaleTransform, HeroPipCount> PipScales{ nullptr, nullptr, nullptr, nullptr, nullptr };
        winrt::Microsoft::UI::Xaml::Style PipStyle{ nullptr };
        winrt::Microsoft::UI::Xaml::Style ActivePipStyle{ nullptr };
    };

    // Draws Home's featured hero the way the reference's FeaturedHero does:
    // every title's backdrop on its own layer, crossfading over 0.4s and faded
    // out at the bottom into the page, the scrim behind the text, the title's
    // logo in place of its name, and the active pip's fill as the carousel's
    // clock. The view model owns which title shows and whether the carousel
    // still rotates; this owns how that is drawn.
    //
    // UI-thread only, owned by the page. Every handler it registers is revoked
    // in its destructor, and nothing it starts outlives it: the clock is a
    // storyboard it stops, and the composition visuals are removed from their
    // hosts. Backdrops are decoded once per run of titles, not per rotation,
    // and each layer other than the shown one and the one fading out of view
    // is hidden, so no backdrop is drawn behind another for longer than a fade.
    class HomeHeroPresenter final
    {
    public:
        HomeHeroPresenter(HomeHeroParts parts, std::function<void()> dwellElapsed);
        ~HomeHeroPresenter();

        HomeHeroPresenter(HomeHeroPresenter const&) = delete;
        HomeHeroPresenter& operator=(HomeHeroPresenter const&) = delete;
        HomeHeroPresenter(HomeHeroPresenter&&) = delete;
        HomeHeroPresenter& operator=(HomeHeroPresenter&&) = delete;

        // Brings the hero in line with the view model. Cheap when nothing it
        // draws changed, so it can follow every property notification.
        void Sync(
            winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> const& items,
            std::int32_t index,
            bool autoAdvance);
        // The pointer rests on the hero: the clock pauses and later resumes
        // where it stopped.
        void SetHovered(bool hovered);
        // Whether Home is on screen. Off screen the clock is stopped; back on,
        // the current title's dwell starts over.
        void SetRunning(bool running);
        void OnThemeChanged();

    private:
        struct ArtLayer final
        {
            std::wstring Url;
            winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface Surface{ nullptr };
            winrt::Microsoft::UI::Composition::SpriteVisual Visual{ nullptr };
            winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface::LoadCompleted_revoker Loaded;
            bool Ready{};
        };

        void BuildComposition();
        void RebuildLayers(std::vector<winrt::HaloDesktop::FeaturedItem> const& items);
        void ReleaseLayers();
        void Activate(std::int32_t index, bool crossfade);
        void UpdatePips(std::vector<winrt::HaloDesktop::FeaturedItem> const& items);
        void UpdateTitleTreatment();
        void UpdatePlaceholder();
        void RestartClock();
        void StopClock();
        void ApplyScrimColors();
        void ResizeArt();
        void ResizeLogoShadow();
        void OnLayerLoaded(winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface const& surface, bool succeeded);
        void OnDwellCompleted();
        [[nodiscard]] bool AnimationsEnabled() const;

        HomeHeroParts m_parts;
        std::function<void()> m_dwellElapsed;
        winrt::Windows::UI::ViewManagement::UISettings m_uiSettings;

        winrt::Microsoft::UI::Composition::Compositor m_compositor{ nullptr };
        winrt::Microsoft::UI::Composition::ContainerVisual m_artRoot{ nullptr };
        winrt::Microsoft::UI::Composition::CompositionRoundedRectangleGeometry m_artClip{ nullptr };
        winrt::Microsoft::UI::Composition::CompositionLinearGradientBrush m_fadeMask{ nullptr };
        winrt::Microsoft::UI::Composition::CompositionLinearGradientBrush m_scrimGradient{ nullptr };
        winrt::Microsoft::UI::Composition::SpriteVisual m_scrim{ nullptr };
        winrt::Microsoft::UI::Composition::ImplicitAnimationCollection m_crossfade{ nullptr };
        winrt::Microsoft::UI::Composition::Visual m_bodyVisual{ nullptr };
        winrt::Microsoft::UI::Composition::ScalarKeyFrameAnimation m_bodyFade{ nullptr };
        winrt::Microsoft::UI::Composition::ScalarKeyFrameAnimation m_bodyRise{ nullptr };
        winrt::Microsoft::UI::Composition::SpriteVisual m_logoShadow{ nullptr };
        winrt::Microsoft::UI::Composition::DropShadow m_logoDropShadow{ nullptr };

        winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard m_clock{ nullptr };
        winrt::Microsoft::UI::Xaml::Media::Animation::DoubleAnimation m_clockFill{ nullptr };

        std::vector<ArtLayer> m_layers;
        // The run of titles the layers were built for, by type, id and art.
        std::vector<std::wstring> m_keys;
        std::vector<winrt::HaloDesktop::FeaturedItem> m_items;
        std::unordered_map<std::wstring, winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage> m_logos;
        std::unordered_set<std::wstring> m_failedLogos;
        std::wstring m_shownLogo;
        std::array<winrt::hstring, HeroPipCount> m_pipTitles;
        std::int32_t m_active{ -1 };
        bool m_autoAdvance{ true };
        bool m_hovered{};
        bool m_running{};
        bool m_clockRunning{};

        // Last, so they are revoked before anything they reach is destroyed.
        winrt::Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker m_artSizeChanged;
        winrt::Microsoft::UI::Xaml::FrameworkElement::SizeChanged_revoker m_logoSizeChanged;
        winrt::Microsoft::UI::Xaml::Controls::Image::ImageOpened_revoker m_logoOpened;
        winrt::Microsoft::UI::Xaml::Controls::Image::ImageFailed_revoker m_logoFailed;
        winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard::Completed_revoker m_clockCompleted;
    };
}
