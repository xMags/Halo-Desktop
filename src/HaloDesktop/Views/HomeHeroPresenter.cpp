#include "pch.h"
#include "Views/HomeHeroPresenter.h"

#include "Views/HeroLogoPolicy.h"


#include <cstdint>
#include <optional>
#include <pplawait.h>
#include <stdexcept>
#include <utility>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.Foundation.Numerics.h>

namespace
{
    using winrt::Microsoft::UI::Xaml::Hosting::ElementCompositionPreview;

    // Backdrops are decoded no larger than this, which still covers the widest
    // hero on a dense display without holding an 8K original in memory.
    constexpr winrt::Windows::Foundation::Size BackdropDecodeLimit{ 2560.0F, 1440.0F };
    constexpr float HeroCornerRadius = 8.0F;
    // The body rises this far into place as it fades in.
    constexpr float BodyRise = 6.0F;
    // `.pip` and `.pip-active`, as HeroPipStyle and HeroActivePipStyle set them.
    constexpr double PipWidth = 8.0;
    constexpr double ActivePipWidth = 28.0;

    winrt::Windows::UI::Color Transparent(winrt::Windows::UI::Color color) noexcept
    {
        // The colour itself at zero alpha, so the gradient fades the scrim out
        // rather than through black, as a premultiplied CSS gradient does.
        color.A = 0;
        return color;
    }

    winrt::Windows::Foundation::TimeSpan ToTimeSpan(std::chrono::milliseconds value) noexcept
    {
        return std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(value);
    }
}

namespace HaloDesktop::Views
{
    HomeHeroPresenter::HomeHeroPresenter(HomeHeroParts parts, std::function<void()> dwellElapsed)
        : m_parts(std::move(parts)), m_dwellElapsed(std::move(dwellElapsed))
    {
        if (!m_parts.Root || !m_parts.ArtHost || !m_parts.Placeholder || !m_parts.Body || !m_parts.Logo
            || !m_parts.LogoShadowHost || !m_parts.LogoFrame || !m_parts.TitleText || !m_parts.PipStrip
            || !m_parts.PipStyle || !m_parts.ActivePipStyle)
        {
            throw std::invalid_argument{ "The hero needs all of its parts." };
        }
        for (std::size_t index = 0; index < HeroPipCount; ++index)
        {
            if (!m_parts.Pips[index] || !m_parts.PipFills[index] || !m_parts.PipScales[index])
            {
                throw std::invalid_argument{ "The hero needs every pip." };
            }
        }

        BuildComposition();

        // The active pip's fill is the carousel's clock: one storyboard, moved
        // onto whichever pip is active. One clock means the fill can never
        // disagree with when the title actually changes.
        m_clockFill = winrt::Microsoft::UI::Xaml::Media::Animation::DoubleAnimation{};
        m_clockFill.From(0.0);
        m_clockFill.To(1.0);
        m_clockFill.Duration(winrt::Microsoft::UI::Xaml::DurationHelper::FromTimeSpan(ToTimeSpan(HeroDwell)));
        m_clock = winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard{};
        m_clock.Children().Append(m_clockFill);
        winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard::SetTargetProperty(m_clockFill, L"ScaleX");

        m_clockCompleted = m_clock.Completed(winrt::auto_revoke, [this](auto const&, auto const&)
        {
            OnDwellCompleted();
        });
        m_artSizeChanged = m_parts.ArtHost.SizeChanged(winrt::auto_revoke, [this](auto const&, auto const&)
        {
            ResizeArt();
        });
        m_logoSizeChanged = m_parts.LogoShadowHost.SizeChanged(winrt::auto_revoke, [this](auto const&, auto const&)
        {
            ResizeLogoShadow();
        });
        m_logoOpened = m_parts.Logo.ImageOpened(winrt::auto_revoke, [this](auto const&, auto const&)
        {
            // The mask follows the picture now on screen.
            if (m_logoDropShadow)
            {
                m_logoDropShadow.Mask(m_parts.Logo.GetAlphaMask());
            }
        });
        m_logoFailed = m_parts.Logo.ImageFailed(winrt::auto_revoke, [this](auto const&, auto const&)
        {
            // A logo that fails to load falls back to the name, and stays
            // fallen back for as long as this run of titles lasts.
            if (!m_shownLogo.empty())
            {
                m_failedLogos.insert(m_shownLogo);
            }
            UpdateTitleTreatment();
        });
    }

    HomeHeroPresenter::~HomeHeroPresenter()
    {
        // The page is cached, so this runs when the process is closing, after the
        // XAML core has gone: calling into a storyboard or an element then is an
        // access violation, which no catch stops. The page stops the clock when
        // it unloads, and the hosts die with the page, so only the composition
        // objects this presenter made are released here.
        try
        {
            ReleaseLayers();
        }
        catch (...)
        {
        }
    }

    void HomeHeroPresenter::BuildComposition()
    {
        m_compositor = ElementCompositionPreview::GetElementVisual(m_parts.ArtHost).Compositor();

        m_artRoot = m_compositor.CreateContainerVisual();
        m_artClip = m_compositor.CreateRoundedRectangleGeometry();
        m_artClip.CornerRadius({ HeroCornerRadius, HeroCornerRadius });
        m_artRoot.Clip(m_compositor.CreateGeometricClip(m_artClip));

        // The art fades out over its lower half into whatever is really behind
        // the hero, the window's glows included, in both themes.
        m_fadeMask = m_compositor.CreateLinearGradientBrush();
        m_fadeMask.StartPoint({ 0.0F, 0.0F });
        m_fadeMask.EndPoint({ 0.0F, 1.0F });
        winrt::Windows::UI::Color const opaque{ 0xFF, 0x00, 0x00, 0x00 };
        m_fadeMask.ColorStops().Append(m_compositor.CreateColorGradientStop(0.0F, opaque));
        m_fadeMask.ColorStops().Append(m_compositor.CreateColorGradientStop(0.5F, opaque));
        m_fadeMask.ColorStops().Append(m_compositor.CreateColorGradientStop(1.0F, Transparent(opaque)));

        m_artMask = m_compositor.CreateSurfaceBrush();
        m_artMask.Stretch(winrt::Microsoft::UI::Composition::CompositionStretch::Fill);
        LoadFadeMask();
        ElementCompositionPreview::SetElementChildVisual(m_parts.ArtHost, m_artRoot);

        // CSS `ease`, the reference's timing for both the crossfade and the body.
        auto const ease = m_compositor.CreateCubicBezierEasingFunction({ 0.25F, 0.1F }, { 0.25F, 1.0F });
        auto const fade = m_compositor.CreateScalarKeyFrameAnimation();
        fade.Target(L"Opacity");
        fade.InsertExpressionKeyFrame(1.0F, L"this.FinalValue", ease);
        fade.Duration(ToTimeSpan(HeroFade));
        m_crossfade = m_compositor.CreateImplicitAnimationCollection();
        m_crossfade.Insert(L"Opacity", fade);

        m_bodyVisual = ElementCompositionPreview::GetElementVisual(m_parts.Body);
        ElementCompositionPreview::SetIsTranslationEnabled(m_parts.Body, true);
        m_bodyFade = m_compositor.CreateScalarKeyFrameAnimation();
        m_bodyFade.InsertKeyFrame(0.0F, 0.0F);
        m_bodyFade.InsertKeyFrame(1.0F, 1.0F, ease);
        m_bodyFade.Duration(ToTimeSpan(HeroFade));
        m_bodyRise = m_compositor.CreateScalarKeyFrameAnimation();
        m_bodyRise.InsertKeyFrame(0.0F, BodyRise);
        m_bodyRise.InsertKeyFrame(1.0F, 0.0F, ease);
        m_bodyRise.Duration(ToTimeSpan(HeroFade));

        // The logo's `drop-shadow(0 4px 24px rgba(0,0,0,.5))`, cast by the
        // logo's own alpha rather than its box.
        m_logoDropShadow = m_compositor.CreateDropShadow();
        m_logoDropShadow.BlurRadius(24.0F);
        m_logoDropShadow.Offset({ 0.0F, 4.0F, 0.0F });
        m_logoDropShadow.Color({ 0x80, 0x00, 0x00, 0x00 });
        m_logoShadow = m_compositor.CreateSpriteVisual();
        m_logoShadow.Shadow(m_logoDropShadow);
        ElementCompositionPreview::SetElementChildVisual(m_parts.LogoShadowHost, m_logoShadow);
    }

    void HomeHeroPresenter::Sync(
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> const& items,
        std::int32_t index,
        bool autoAdvance)
    {
        std::vector<winrt::HaloDesktop::FeaturedItem> featured;
        std::vector<std::wstring> keys;
        if (items)
        {
            for (auto const& entry : items)
            {
                auto const item = entry.try_as<winrt::HaloDesktop::FeaturedItem>();
                if (!item || !item.Media() || featured.size() == HeroPipCount)
                {
                    continue;
                }
                keys.push_back(std::wstring{ item.Media().Type() } + L':' + std::wstring{ item.Media().Id() }
                    + L'\x1F' + std::wstring{ item.Background() });
                featured.push_back(item);
            }
        }

        auto const listChanged = keys != m_keys;
        m_items = featured;
        if (listChanged)
        {
            m_keys = std::move(keys);
            m_logos.clear();
            m_failedLogos.clear();
            m_pendingLogos.clear();
            ++m_logoGeneration;
            m_shownLogo.clear();
            PreloadLogos(featured);
            m_active = -1;
            RebuildLayers(featured);
            ResizeArt();
        }

        if (featured.empty())
        {
            StopClock();
            UpdatePips(featured, false);
            UpdatePlaceholder();
            return;
        }

        auto const count = static_cast<std::int32_t>(featured.size());
        auto const target = index >= 0 && index < count ? index : 0;
        auto const indexChanged = target != m_active;
        if (indexChanged)
        {
            Activate(target, m_active >= 0);
        }
        auto const modeChanged = autoAdvance != m_autoAdvance;
        m_autoAdvance = autoAdvance;
        UpdatePips(featured, !listChanged && AnimationsEnabled());
        UpdateTitleTreatment();
        if (listChanged || indexChanged || modeChanged)
        {
            RestartClock();
        }
    }

    void HomeHeroPresenter::RebuildLayers(std::vector<winrt::HaloDesktop::FeaturedItem> const& items)
    {
        ReleaseLayers();
        m_layers.reserve(items.size());
        for (auto const& item : items)
        {
            ArtLayer layer;
            layer.Url = std::wstring{ item.Background() };
            layer.Visual = m_compositor.CreateSpriteVisual();
            layer.Visual.Opacity(0.0F);
            layer.Visual.IsVisible(false);
            if (!layer.Url.empty())
            {
                try
                {
                    // Loaded and decoded off the UI thread, once for the whole
                    // run of titles: rotating back to a title reuses it.
                    layer.Surface = winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface::StartLoadFromUri(
                        winrt::Windows::Foundation::Uri{ layer.Url },
                        BackdropDecodeLimit);
                    auto const art = m_compositor.CreateSurfaceBrush(layer.Surface);
                    art.Stretch(winrt::Microsoft::UI::Composition::CompositionStretch::UniformToFill);
                    art.HorizontalAlignmentRatio(0.5F);
                    art.VerticalAlignmentRatio(0.5F);
                    // Faded out over its lower half and toward the text side
                    // by one mask: a mask brush cannot take another as its
                    // source, so the two fades are one image (LoadFadeMask).
                    auto const masked = m_compositor.CreateMaskBrush();
                    masked.Source(art);
                    masked.Mask(m_maskFailed
                        ? m_fadeMask.as<winrt::Microsoft::UI::Composition::CompositionBrush>()
                        : m_artMask.as<winrt::Microsoft::UI::Composition::CompositionBrush>());
                    layer.Masked = masked;
                    layer.Visual.Brush(masked);
                    layer.Loaded = layer.Surface.LoadCompleted(winrt::auto_revoke, [this](
                        winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface const& surface,
                        winrt::Microsoft::UI::Xaml::Media::LoadedImageSourceLoadCompletedEventArgs const& args)
                    {
                        OnLayerLoaded(
                            surface,
                            args.Status() == winrt::Microsoft::UI::Xaml::Media::LoadedImageSourceLoadStatus::Success);
                    });
                }
                catch (...)
                {
                    // A URL the loader refuses leaves the placeholder showing,
                    // as a backdrop that fails to load does.
                    layer.Surface = nullptr;
                    layer.Visual.Brush(nullptr);
                }
            }
            layer.Visual.ImplicitAnimations(m_crossfade);
            m_artRoot.Children().InsertAtTop(layer.Visual);
            m_layers.push_back(std::move(layer));
        }
    }

    void HomeHeroPresenter::ReleaseLayers()
    {
        for (auto& layer : m_layers)
        {
            layer.Loaded.revoke();
            if (layer.Visual && m_artRoot)
            {
                m_artRoot.Children().Remove(layer.Visual);
            }
            if (layer.Surface)
            {
                layer.Surface.Close();
            }
        }
        m_layers.clear();
    }

    void HomeHeroPresenter::Activate(std::int32_t index, bool crossfade)
    {
        auto const previous = m_active;
        for (std::int32_t layerIndex = 0; layerIndex < static_cast<std::int32_t>(m_layers.size()); ++layerIndex)
        {
            auto const& visual = m_layers[static_cast<std::size_t>(layerIndex)].Visual;
            auto const shown = layerIndex == index;
            // The outgoing layer stays drawn while it fades; every other one is
            // hidden outright, so at most two backdrops are ever composed.
            auto const leaving = crossfade && layerIndex == previous && previous != index;
            if (!crossfade)
            {
                visual.ImplicitAnimations(nullptr);
            }
            visual.IsVisible(shown || leaving);
            visual.Opacity(shown ? 1.0F : 0.0F);
            if (!crossfade)
            {
                visual.ImplicitAnimations(m_crossfade);
            }
        }
        m_active = index;
        UpdatePlaceholder();

        // The text arrives with its picture rather than ahead of it.
        if (m_bodyVisual && AnimationsEnabled())
        {
            m_bodyVisual.StartAnimation(L"Opacity", m_bodyFade);
            m_bodyVisual.StartAnimation(L"Translation.Y", m_bodyRise);
        }
    }

    void HomeHeroPresenter::UpdatePips(std::vector<winrt::HaloDesktop::FeaturedItem> const& items, bool animateWidths)
    {
        m_parts.PipStrip.Visibility(items.size() > 1
            ? winrt::Microsoft::UI::Xaml::Visibility::Visible
            : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        // Where each pip is drawn now, read before restyling moves any of them.
        std::array<double, HeroPipCount> from{};
        for (std::size_t index = 0; index < HeroPipCount; ++index)
        {
            from[index] = m_parts.Pips[index].ActualWidth();
        }
        bool restyled{};
        for (std::size_t index = 0; index < HeroPipCount; ++index)
        {
            auto const& pip = m_parts.Pips[index];
            if (index >= items.size())
            {
                pip.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
                continue;
            }
            pip.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            auto const active = static_cast<std::int32_t>(index) == m_active;
            auto const& style = active ? m_parts.ActivePipStyle : m_parts.PipStyle;
            // Restyling re-applies the template, so only on a real change.
            if (pip.Style() != style)
            {
                pip.Style(style);
                restyled = true;
            }
            m_parts.PipFills[index].Visibility(active
                ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            // Named only when the title changes: this runs on every sync.
            auto const title = items[index].Title();
            if (m_pipTitles[index] != title)
            {
                m_pipTitles[index] = title;
                winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(pip, L"Show " + title);
                winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(pip, winrt::box_value(title));
            }
        }
        if (!restyled)
        {
            return;
        }
        if (!animateWidths)
        {
            // Back to the widths the styles set.
            if (m_pipWidths)
            {
                m_pipWidths.Stop();
                m_pipWidths = nullptr;
            }
            return;
        }
        AnimatePipWidths(items.size(), from);
    }

    void HomeHeroPresenter::AnimatePipWidths(std::size_t count, std::array<double, HeroPipCount> const& from)
    {
        namespace Animation = winrt::Microsoft::UI::Xaml::Media::Animation;
        if (m_pipWidths)
        {
            m_pipWidths.Stop();
        }
        m_pipWidths = Animation::Storyboard{};
        auto const resize = winrt::Microsoft::UI::Xaml::Media::Animation::KeyTimeHelper::FromTimeSpan(ToTimeSpan(HeroPipResize));
        auto const start = winrt::Microsoft::UI::Xaml::Media::Animation::KeyTimeHelper::FromTimeSpan(winrt::Windows::Foundation::TimeSpan{});
        for (std::size_t index = 0; index < count && index < HeroPipCount; ++index)
        {
            auto const target = static_cast<std::int32_t>(index) == m_active ? ActivePipWidth : PipWidth;
            // A pip not drawn yet has no width to start from: it starts where
            // it would have rested.
            auto const origin = from[index] > 0.0
                ? from[index]
                : (target == ActivePipWidth ? PipWidth : ActivePipWidth);
            Animation::DoubleAnimationUsingKeyFrames animation;
            // Width is a layout property, so this runs on the UI thread; it is
            // five pips for 0.3s.
            animation.EnableDependentAnimation(true);
            Animation::DiscreteDoubleKeyFrame first;
            first.KeyTime(start);
            first.Value(origin);
            animation.KeyFrames().Append(first);
            // CSS `ease`, the reference's timing.
            Animation::KeySpline spline;
            spline.ControlPoint1({ 0.25F, 0.1F });
            spline.ControlPoint2({ 0.25F, 1.0F });
            Animation::SplineDoubleKeyFrame last;
            last.KeyTime(resize);
            last.Value(target);
            last.KeySpline(spline);
            animation.KeyFrames().Append(last);
            Animation::Storyboard::SetTarget(animation, m_parts.Pips[index]);
            Animation::Storyboard::SetTargetProperty(animation, L"Width");
            m_pipWidths.Children().Append(animation);
        }
        m_pipWidths.Begin();
    }

    void HomeHeroPresenter::PreloadLogos(std::vector<winrt::HaloDesktop::FeaturedItem> const& items)
    {
        for (auto const& item : items)
        {
            std::wstring url{ item.Logo() };
            if (url.empty() || m_logos.contains(url) || m_failedLogos.contains(url) || m_pendingLogos.contains(url)) continue;
            if (!::HaloDesktop::Views::IsHeroLogoUrl(url))
            {
                m_failedLogos.insert(url);
                continue;
            }
            m_pendingLogos.insert(url);
            PreloadLogo(std::move(url), m_logoGeneration);
        }
    }

    winrt::fire_and_forget HomeHeroPresenter::PreloadLogo(std::wstring url, std::uint64_t generation)
    {
        // Copied before the first suspension: `this` may be gone by the end.
        std::weak_ptr<bool> const alive = m_alive;
        auto const uiContext = winrt::apartment_context{};
        if (!m_logoClient) m_logoClient = winrt::Windows::Web::Http::HttpClient{};
        auto const client = m_logoClient;
        std::optional<LoadedHeroLogo> loaded;
        try
        {
            loaded = co_await LoadHeroLogoAsync(client, url);
        }
        catch (...)
        {
            loaded.reset();
        }
        // Back on the UI thread, where this presenter is destroyed, so the
        // check below cannot race its destruction.
        co_await uiContext;
        if (alive.expired() || generation != m_logoGeneration) co_return;
        m_pendingLogos.erase(url);
        if (loaded && loaded->Image)
        {
            m_logos.insert_or_assign(url, std::move(*loaded));
        }
        else
        {
            m_failedLogos.insert(url);
        }
        UpdateTitleTreatment();
    }

    // The logo art when the title has one, it has loaded and it reads over the
    // hero (HeroLogoReadable), else the name. Logos are drawn for dark
    // backdrops, so in the light theme a mostly white one keeps the name.
    void HomeHeroPresenter::UpdateTitleTreatment()
    {
        auto const item = m_active >= 0 && static_cast<std::size_t>(m_active) < m_items.size()
            ? m_items[static_cast<std::size_t>(m_active)]
            : winrt::HaloDesktop::FeaturedItem{ nullptr };
        std::wstring const url = item ? std::wstring{ item.Logo() } : std::wstring{};
        auto const light = m_parts.Root.ActualTheme() == winrt::Microsoft::UI::Xaml::ElementTheme::Light;
        auto const ready = url.empty() || m_failedLogos.contains(url) ? m_logos.end() : m_logos.find(url);
        auto const showLogo = ready != m_logos.end()
            && ::HaloDesktop::Views::HeroLogoReadable(ready->second.Luminance, light);
        if (showLogo && url != m_shownLogo)
        {
            m_shownLogo = url;
            m_parts.Logo.Source(ready->second.Image);
        }
        if (!showLogo)
        {
            m_shownLogo.clear();
        }
        m_parts.LogoFrame.Visibility(showLogo
            ? winrt::Microsoft::UI::Xaml::Visibility::Visible
            : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        m_parts.TitleText.Visibility(showLogo
            ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
            : winrt::Microsoft::UI::Xaml::Visibility::Visible);
    }

    // The bare placeholder shows behind the title while the shown backdrop
    // loads, and goes once it is up: left behind a loaded backdrop it would be
    // drawn under it, full width, for as long as the hero is on screen.
    void HomeHeroPresenter::UpdatePlaceholder()
    {
        // The art draws nothing until its fade mask has decoded, so both count.
        auto const ready = m_maskReady
            && m_active >= 0
            && static_cast<std::size_t>(m_active) < m_layers.size()
            && m_layers[static_cast<std::size_t>(m_active)].Ready;
        m_parts.Placeholder.Visibility(ready
            ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
            : winrt::Microsoft::UI::Xaml::Visibility::Visible);
    }

    void HomeHeroPresenter::OnLayerLoaded(
        winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface const& surface,
        bool succeeded)
    {
        for (auto& layer : m_layers)
        {
            if (layer.Surface == surface)
            {
                layer.Ready = succeeded;
                break;
            }
        }
        UpdatePlaceholder();
    }

    void HomeHeroPresenter::RestartClock()
    {
        StopClock();
        if (m_active < 0 || static_cast<std::size_t>(m_active) >= HeroPipCount)
        {
            return;
        }
        auto const& scale = m_parts.PipScales[static_cast<std::size_t>(m_active)];
        // Once the viewer has picked a title the fill sits full: the carousel
        // has stopped.
        if (!m_autoAdvance || m_items.size() < 2)
        {
            scale.ScaleX(1.0);
            return;
        }
        scale.ScaleX(0.0);
        if (!m_running)
        {
            return;
        }
        winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard::SetTarget(m_clockFill, scale);
        m_clock.Begin();
        m_clockRunning = true;
        if (m_hovered)
        {
            m_clock.Pause();
        }
    }

    void HomeHeroPresenter::StopClock()
    {
        m_clockRunning = false;
        if (m_clock)
        {
            m_clock.Stop();
        }
    }

    void HomeHeroPresenter::OnDwellCompleted()
    {
        // Completed is raised once the fill has run its whole dwell. A stop or
        // a restart in between clears the flag or starts a new run, so a stale
        // completion cannot move the carousel on.
        if (!m_clockRunning
            || m_clock.GetCurrentState() != winrt::Microsoft::UI::Xaml::Media::Animation::ClockState::Filling)
        {
            return;
        }
        m_clockRunning = false;
        if (m_dwellElapsed)
        {
            m_dwellElapsed();
        }
    }

    void HomeHeroPresenter::SetHovered(bool hovered)
    {
        m_hovered = hovered;
        if (!m_clockRunning)
        {
            return;
        }
        if (hovered)
        {
            m_clock.Pause();
        }
        else
        {
            m_clock.Resume();
        }
    }

    void HomeHeroPresenter::SetRunning(bool running)
    {
        m_running = running;
        if (!running)
        {
            // The pointer cannot be over a hero that is not on screen, and the
            // exit that would have said so may never arrive.
            m_hovered = false;
            StopClock();
            return;
        }
        RestartClock();
    }

    void HomeHeroPresenter::OnThemeChanged()
    {
        LoadFadeMask();
        UpdateTitleTreatment();
    }

    // The reference lays a scrim of the page colour over the art's text side.
    // Over an opaque page that is the same as fading the art out toward that
    // side, but over the translucent window the scrim's near-solid page colour
    // showed as a pale (or dark) block in the hero's corner. So the art itself
    // fades instead, toward its text side and over its lower half: where the
    // scrim covered a share of the art, that share is let through to whatever
    // is really behind the hero. A mask brush takes one mask and cannot nest,
    // so both fades are baked into one image per theme, drawn from the
    // reference's scrim stops (`--hs1`/`--hs2`: F5 then 9E alpha at 52%, clear by
    // 92%; light: F5 to 24%, 80 at 46%, clear by 68%) and its bottom fade
    // (opaque to 50%, clear at the bottom). Stretched over the hero.
    void HomeHeroPresenter::LoadFadeMask()
    {
        if (!m_artMask || m_maskFailed)
        {
            return;
        }
        auto const light = m_parts.Root.ActualTheme() == winrt::Microsoft::UI::Xaml::ElementTheme::Light;
        if (m_maskRequested && light == m_maskLight)
        {
            return;
        }
        m_maskRequested = true;
        m_maskLight = light;
        try
        {
            m_maskLoaded.revoke();
            m_maskSurface = winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface::StartLoadFromUri(
                winrt::Windows::Foundation::Uri{ light ? L"ms-appx:///Assets/hero-fade-light.png" : L"ms-appx:///Assets/hero-fade-dark.png" });
            m_maskLoaded = m_maskSurface.LoadCompleted(winrt::auto_revoke, [this](
                winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface const& surface,
                winrt::Microsoft::UI::Xaml::Media::LoadedImageSourceLoadCompletedEventArgs const& args)
            {
                OnFadeMaskLoaded(
                    surface,
                    args.Status() == winrt::Microsoft::UI::Xaml::Media::LoadedImageSourceLoadStatus::Success);
            });
        }
        catch (...)
        {
            OnFadeMaskLoaded(nullptr, false);
        }
    }

    void HomeHeroPresenter::OnFadeMaskLoaded(
        winrt::Microsoft::UI::Xaml::Media::LoadedImageSurface const& surface,
        bool succeeded)
    {
        if (succeeded && surface && surface == m_maskSurface)
        {
            // Swapped only once decoded, so a theme change never blanks the art.
            m_artMask.Surface(surface);
            m_maskReady = true;
            UpdatePlaceholder();
            return;
        }
        if (succeeded)
        {
            return; // A load the theme has since replaced.
        }
        // Without the image the art keeps the bottom fade alone, as before the
        // side fade existed, rather than not drawing at all.
        OutputDebugStringW(L"Halo could not load the hero's fade mask; the hero fades at the bottom only.\n");
        m_maskFailed = true;
        m_maskLoaded.revoke();
        for (auto const& layer : m_layers)
        {
            if (layer.Masked)
            {
                layer.Masked.Mask(m_fadeMask);
            }
        }
        m_maskReady = true;
        UpdatePlaceholder();
    }

    void HomeHeroPresenter::ResizeArt()
    {
        auto const size = m_parts.ArtHost.ActualSize();
        if (m_artRoot) m_artRoot.Size(size);
        if (m_artClip) m_artClip.Size(size);
        for (auto const& layer : m_layers)
        {
            layer.Visual.Size(size);
        }
    }

    void HomeHeroPresenter::ResizeLogoShadow()
    {
        if (m_logoShadow)
        {
            m_logoShadow.Size(m_parts.LogoShadowHost.ActualSize());
        }
    }

    bool HomeHeroPresenter::AnimationsEnabled() const
    {
        try
        {
            return m_uiSettings.AnimationsEnabled();
        }
        catch (...)
        {
            return true;
        }
    }
}
