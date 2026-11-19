#include "pch.h"
#include "Controls/ContinueCard.xaml.h"
#if __has_include("ContinueCard.g.cpp")
#include "ContinueCard.g.cpp"
#endif

#include "App.xaml.h"
#include "Controls/ArtworkImage.xaml.h"
#include "Controls/TagChip.xaml.h"
#include "Shell/LayoutMetricsService.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <winrt/Windows.Foundation.Numerics.h>

namespace winrt::HaloDesktop::implementation
{
    // Sized before the first measure, for the reason spelled out in PosterCard.
    ContinueCard::ContinueCard()
    {
        InitializeComponent();
        Microsoft::UI::Xaml::Vector3Transition lift;
        lift.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
            ::HaloDesktop::Controls::CardHoverDuration));
        TranslationTransition(lift);
        ApplyLayoutMetrics();
    }

    winrt::hstring ContinueCard::Title() const { return m_title; }
    void ContinueCard::Title(winrt::hstring const& value)
    {
        m_title = value;
        if (auto text = FindName(L"TitleText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>()) text.Text(value);
    }

    winrt::hstring ContinueCard::Meta() const { return m_meta; }
    void ContinueCard::Meta(winrt::hstring const& value)
    {
        m_meta = value;
        if (auto text = FindName(L"MetaText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>()) text.Text(value);
    }

    winrt::hstring ContinueCard::Art() const { return m_art; }
    void ContinueCard::Art(winrt::hstring const& value)
    {
        m_art = value;
        auto still = FindName(L"Still").try_as<winrt::HaloDesktop::ArtworkImage>();
        if (!still) return;
        still.DecodeWidth(::HaloDesktop::Shell::ContinueDecodeWidth());
        still.SourceUrl(value);
    }

    winrt::hstring ContinueCard::EpisodeTag() const { return m_episodeTag; }
    void ContinueCard::EpisodeTag(winrt::hstring const& value)
    {
        m_episodeTag = value;
        if (auto chip = FindName(L"EpisodeChip").try_as<winrt::HaloDesktop::TagChip>()) chip.Text(value);
    }

    winrt::hstring ContinueCard::TimeLeft() const { return m_timeLeft; }
    void ContinueCard::TimeLeft(winrt::hstring const& value)
    {
        m_timeLeft = value;
        if (auto chip = FindName(L"TimeLeftChip").try_as<winrt::HaloDesktop::TagChip>()) chip.Text(value);
    }

    double ContinueCard::Progress() const noexcept { return m_progress; }
    // Clamped here rather than left to the track. The fraction arrives from
    // recorded playback position over runtime, so a stale or zero runtime can
    // hand us a value outside 0..1 or a NaN, and star widths take whatever they
    // are given: a negative one throws, and a NaN lays out as nothing at all.
    void ContinueCard::Progress(double value)
    {
        m_progress = std::isnan(value) ? 0.0 : std::clamp(value, 0.0, 1.0);
        ApplyProgress();
    }

    // The generated accessors read the field the XAML loader already populated,
    // where FindName walks the namescope on every call. This runs once per card
    // realisation and the strip re-realises on every scroll.
    void ContinueCard::ApplyProgress()
    {
        auto const filled = ProgressFilled();
        auto const remaining = ProgressRemaining();
        if (!filled || !remaining) return;
        using Microsoft::UI::Xaml::GridLengthHelper;
        using Microsoft::UI::Xaml::GridUnitType;
        filled.Width(GridLengthHelper::FromValueAndType(m_progress, GridUnitType::Star));
        remaining.Width(GridLengthHelper::FromValueAndType(1.0 - m_progress, GridUnitType::Star));
    }

    double ContinueCard::CardWidth() const noexcept { return m_cardWidth; }

    void ContinueCard::OnLoaded(
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

    void ContinueCard::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_metricsToken == 0) return;
        if (auto const metrics = App::Services().LayoutMetrics) metrics->RemoveChangedHandler(m_metricsToken);
        m_metricsToken = 0;
    }

    void ContinueCard::ApplyLayoutMetrics()
    {
        auto const metrics = App::Services().LayoutMetrics;
        if (!metrics) return;
        auto const current = metrics->Current();
        // No width of its own: the slot decides, and never below the step's card.
        MinWidth(current.ContinueWidth);
        HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
        ApplyWidth((std::max)(ActualWidth(), current.ContinueWidth));
    }

    void ContinueCard::OnSizeChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        ApplyWidth(args.NewSize().Width);
    }

    // The still keeps 16:9 at whatever width the card has. Height follows width
    // only, so this settles in one pass.
    void ContinueCard::ApplyWidth(double width)
    {
        constexpr double StillAspect = 9.0 / 16.0;
        if (!std::isfinite(width) || width <= 0.0) return;
        m_cardWidth = width;
        auto const height = std::round(width * StillAspect);
        if (auto const row = FindName(L"ArtRow").try_as<Microsoft::UI::Xaml::Controls::RowDefinition>();
            row && row.Height().Value != height)
        {
            row.Height(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(height));
        }
    }

    winrt::event_token ContinueCard::Click(Microsoft::UI::Xaml::RoutedEventHandler const& handler) { return m_click.add(handler); }
    void ContinueCard::Click(winrt::event_token const& token) noexcept { m_click.remove(token); }
    void ContinueCard::OnCardClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_click(*this, args);
    }

    // The same hover as PosterCard: the two sit side by side on Home, and a strip
    // where only half the cards answer the pointer reads as the other half being
    // disabled.
    void ContinueCard::OnCardPointerEntered(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        SetHovered(true);
    }

    void ContinueCard::OnCardPointerExited(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        SetHovered(false);
    }

    // Every part fades on its own transition, so this only sets where each ends up.
    void ContinueCard::SetHovered(bool hovered)
    {
        if (hovered)
        {
            m_hoverShadow.Prepare(ShadowHost(), *this);
        }
        Translation({ 0.0F, hovered ? ::HaloDesktop::Controls::CardHoverLift : 0.0F, 0.0F });
        if (auto const ring = ArtHoverRing()) ring.Opacity(hovered ? 1.0 : 0.0);
        if (auto const rest = ArtRestRing()) rest.Opacity(hovered ? 0.0 : 1.0);
        if (auto const fade = ShadowFade()) fade.Opacity(hovered ? 1.0 : 0.0);
    }
}
