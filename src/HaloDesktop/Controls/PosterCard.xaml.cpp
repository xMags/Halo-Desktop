#include "pch.h"
#include "Controls/PosterCard.xaml.h"
#if __has_include("PosterCard.g.cpp")
#include "PosterCard.g.cpp"
#endif

#include "App.xaml.h"
#include "Controls/ThemeColorLookup.h"
#include "Shell/LayoutMetricsService.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace
{
    // The reference's hover: the card lifts 2px over 120ms while its art takes
    // the hover stroke and `box-shadow: 0 8px 16px -6px`. The negative spread
    // is drawn by insetting the shadow's shape, which keeps it off the strip's
    // clipped sides.
    constexpr std::chrono::milliseconds HoverDuration{ 120 };
    constexpr float HoverLift = -2.0F;
    constexpr float ShadowOffsetY = 8.0F;
    constexpr float ShadowBlur = 16.0F;
    constexpr float ShadowInset = 6.0F;
    // The 2:3 poster frame.
    constexpr double PosterAspect = 1.5;
}

namespace winrt::HaloDesktop::implementation
{
    // Sized in the constructor rather than in Loaded. Loaded fires after the card
    // has already been measured at the width baked into the markup, so sizing there
    // cost a second measure of the card, its shelf and everything under it on every
    // realisation, and handed the shelf list a height that then changed. StackLayout
    // estimates the shelves it has not realised from the ones it has, so a height
    // that moves makes the scroll extent move with it, mid-gesture.
    PosterCard::PosterCard()
    {
        InitializeComponent();
        Microsoft::UI::Xaml::Vector3Transition lift;
        lift.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(HoverDuration));
        TranslationTransition(lift);
        ApplyLayoutMetrics();
    }

    winrt::hstring PosterCard::Title() const { return m_title; }
    void PosterCard::Title(winrt::hstring const& value) { m_title = value; if (auto text = FindName(L"TitleText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>()) text.Text(value); }
    winrt::hstring PosterCard::Meta() const { return m_meta; }
    void PosterCard::Meta(winrt::hstring const& value) { m_meta = value; if (auto text = FindName(L"MetaText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>()) text.Text(value); }
    winrt::hstring PosterCard::Poster() const { return m_poster; }
    void PosterCard::Poster(winrt::hstring const& value)
    {
        auto const image = FindName(L"PosterImage").try_as<Microsoft::UI::Xaml::Controls::Image>();
        // Recycled cards are handed the same url they already hold often enough
        // that rebuilding the bitmap here is a decode per scroll, not per item.
        if (m_poster == value && image && image.Source() != nullptr) return;
        m_poster = value;
        if (!image) return;
        image.Opacity(0);
        ShowPlaceholder(true);
        if (value.empty()) { image.Source(nullptr); return; }
        try
        {
            Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
            // Set before the uri, or the decode has already been scheduled at full
            // size. Poster sources run to a thousand pixels wide for a slot a sixth
            // of that, and every realised card was holding the whole thing.
            bitmap.DecodePixelWidth(::HaloDesktop::Shell::PosterDecodeWidth());
            bitmap.UriSource(winrt::Windows::Foundation::Uri{ value });
            image.Source(bitmap);
        }
        catch (...) { image.Source(nullptr); }
    }
    winrt::hstring PosterCard::KindLabel() const { return m_kindLabel; }
    void PosterCard::KindLabel(winrt::hstring const& value) { m_kindLabel = value; if (auto text = FindName(L"KindText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>()) text.Text(value); }
    bool PosterCard::ShowKindBadge() const noexcept { return m_showKindBadge; }
    void PosterCard::ShowKindBadge(bool value)
    {
        m_showKindBadge = value;
        if (auto badge = FindName(L"KindBadge").try_as<Microsoft::UI::Xaml::Controls::Border>())
        {
            badge.Visibility(value ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed);
        }
    }

    bool PosterCard::FillsColumn() const noexcept { return m_fillsColumn; }
    void PosterCard::FillsColumn(bool value)
    {
        if (m_fillsColumn == value) return;
        m_fillsColumn = value;
        if (value)
        {
            // The art follows the width the column hands the card, so the frame
            // stays 2:3 however far the column stretches.
            m_sizeChanged = SizeChanged(winrt::auto_revoke, [this](auto const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
            {
                ApplyColumnWidth(args.NewSize().Width);
            });
        }
        else
        {
            m_sizeChanged.revoke();
        }
        ApplyLayoutMetrics();
    }

    double PosterCard::CardWidth() const noexcept { return m_cardWidth; }

    // The card reads the layout step itself rather than being fed by its hosts.
    // It sits inside a DataTemplate in three different list controls, two of them
    // nested a second template deep, and XAML gives a template no way to see past
    // its own item. Pushing the size down would mean a bespoke walk of realised
    // containers per host; subscribing here is one place instead of three.
    void PosterCard::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_metricsToken != 0) return;
        auto const metrics = App::Services().LayoutMetrics;
        if (!metrics) return;
        m_metricsToken = metrics->AddChangedHandler([weak = get_weak()]()
        {
            if (auto self = weak.get()) self->ApplyLayoutMetrics();
        });
        ApplyLayoutMetrics();
    }

    // Recycled cards are unloaded, so the handler has to go with them or the
    // service accumulates one entry per card the user ever scrolled past.
    void PosterCard::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_metricsToken == 0) return;
        if (auto const metrics = App::Services().LayoutMetrics) metrics->RemoveChangedHandler(m_metricsToken);
        m_metricsToken = 0;
    }

    void PosterCard::ApplyLayoutMetrics()
    {
        auto const metrics = App::Services().LayoutMetrics;
        if (!metrics) return;
        auto const current = metrics->Current();
        m_cardWidth = current.PosterWidth;
        if (m_fillsColumn)
        {
            // No width of its own: the column decides, and never below a poster.
            Width(std::numeric_limits<double>::quiet_NaN());
            MinWidth(current.PosterWidth);
            HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
            ApplyColumnWidth(ActualWidth() > 0.0 ? ActualWidth() : current.PosterWidth);
            return;
        }
        MinWidth(0.0);
        Width(current.PosterWidth);
        if (auto const row = FindName(L"ArtRow").try_as<Microsoft::UI::Xaml::Controls::RowDefinition>())
        {
            row.Height(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(current.PosterArtHeight()));
        }
    }

    void PosterCard::ApplyColumnWidth(double width)
    {
        if (!std::isfinite(width) || width <= 0.0) return;
        m_cardWidth = width;
        auto const row = ArtRow();
        auto const height = std::round(width * PosterAspect);
        // Height follows width only, so this settles in one pass.
        if (row && row.Height().Value != height)
        {
            row.Height(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(height));
        }
    }

    winrt::event_token PosterCard::Click(Microsoft::UI::Xaml::RoutedEventHandler const& handler) { return m_click.add(handler); }
    void PosterCard::Click(winrt::event_token const& token) noexcept { m_click.remove(token); }
    void PosterCard::OnCardClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_click(*this, args); }
    void PosterCard::OnCardPointerEntered([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        SetHovered(true);
    }
    void PosterCard::OnCardPointerExited([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        SetHovered(false);
    }

    // Every part fades on its own transition, so this only sets where each
    // ends up. Opacity is a plain double, which leaves the rings' themed
    // brushes bound to the theme.
    void PosterCard::SetHovered(bool hovered)
    {
        if (hovered)
        {
            EnsureHoverShadow();
        }
        Translation({ 0.0F, hovered ? HoverLift : 0.0F, 0.0F });
        if (auto const ring = ArtHoverRing()) ring.Opacity(hovered ? 1.0 : 0.0);
        if (auto const rest = ArtRestRing()) rest.Opacity(hovered ? 0.0 : 1.0);
        if (auto const host = ShadowHost()) host.Opacity(hovered ? 1.0 : 0.0);
    }

    void PosterCard::EnsureHoverShadow()
    {
        auto const host = ShadowHost();
        if (!host) return;
        if (!m_shadowVisual)
        {
            auto const compositor = Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(host).Compositor();
            m_shadow = compositor.CreateDropShadow();
            m_shadow.BlurRadius(ShadowBlur);
            m_shadow.Offset({ 0.0F, ShadowOffsetY, 0.0F });
            // A brushless sprite casts the shadow of its own rectangle and
            // draws nothing else; the art's rounded corners lie outside the
            // inset rectangle, so none of them peeks out.
            m_shadowVisual = compositor.CreateSpriteVisual();
            m_shadowVisual.Shadow(m_shadow);
            m_shadowVisual.Offset({ ShadowInset, ShadowInset, 0.0F });
            Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(host, m_shadowVisual);
            m_shadowSizeChanged = host.SizeChanged(winrt::auto_revoke, [this](auto const&, auto const&)
            {
                SizeHoverShadow();
            });
            SizeHoverShadow();
        }
        // Read on every hover, so a theme switch since the last one is honoured.
        if (auto const color = ::HaloDesktop::Controls::ThemeColor(*this, L"HaloHoverCardShadowColor"))
        {
            m_shadow.Color(*color);
        }
    }

    void PosterCard::SizeHoverShadow()
    {
        auto const host = ShadowHost();
        if (!m_shadowVisual || !host) return;
        auto const size = host.ActualSize();
        m_shadowVisual.Size({
            (std::max)(0.0F, size.x - 2.0F * ShadowInset),
            (std::max)(0.0F, size.y - 2.0F * ShadowInset) });
    }
    // The placeholder is opaque and sits directly behind the artwork, so leaving
    // it in place made every card in the list draw its art twice.
    void PosterCard::ShowPlaceholder(bool visible)
    {
        if (auto const placeholder = FindName(L"Placeholder").try_as<Microsoft::UI::Xaml::UIElement>())
        {
            placeholder.Visibility(visible ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed);
        }
    }
    void PosterCard::OnPosterOpened(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto image = FindName(L"PosterImage").try_as<Microsoft::UI::Xaml::Controls::Image>()) image.Opacity(1);
        ShowPlaceholder(false);
    }
    void PosterCard::OnPosterFailed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::ExceptionRoutedEventArgs const&)
    {
        if (auto image = FindName(L"PosterImage").try_as<Microsoft::UI::Xaml::Controls::Image>()) image.Opacity(0);
    }
}
