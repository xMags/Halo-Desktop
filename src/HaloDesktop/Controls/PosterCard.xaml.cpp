#include "pch.h"
#include "Controls/PosterCard.xaml.h"
#if __has_include("PosterCard.g.cpp")
#include "PosterCard.g.cpp"
#endif

#include "App.xaml.h"
#include "Controls/ArtworkUrlPolicy.h"
#include "Controls/PressFeedback.h"
#include "Shell/LayoutMetricsService.h"

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <string_view>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace
{
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
        lift.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
            ::HaloDesktop::Controls::CardHoverDuration));
        TranslationTransition(lift);
        // A press sinks the whole card, lift and all, and springs it back.
        ::HaloDesktop::Controls::AttachPressFeedback(*this, CardButton());
        ApplyLayoutMetrics();
    }

    winrt::hstring PosterCard::Title() const { return m_title; }
    // The card's text is set from here, so its button has no name of its own
    // for a screen reader: it takes the title.
    void PosterCard::Title(winrt::hstring const& value)
    {
        m_title = value;
        if (auto text = FindName(L"TitleText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>()) text.Text(value);
        if (auto button = FindName(L"CardButton").try_as<Microsoft::UI::Xaml::Controls::Button>())
        {
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(button, value);
        }
    }
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
        LoadArtwork();
    }

    winrt::hstring PosterCard::MetaId() const { return m_metaId; }
    void PosterCard::MetaId(winrt::hstring const& value)
    {
        if (m_metaId == value) return;
        m_metaId = value;
        // Bindings set the id and the poster in either order, and a recycled card
        // gets both anew. Only a card showing its own poster can keep it; one on
        // a fallback, or with nothing at all, starts over for the new id.
        auto const onOwnPoster = !m_exhausted && !m_poster.empty()
            && m_attempted.size() == 1 && m_attempted.front() == std::wstring_view{ m_poster };
        if (!onOwnPoster) LoadArtwork();
    }

    void PosterCard::LoadArtwork()
    {
        auto const image = FindName(L"PosterImage").try_as<Microsoft::UI::Xaml::Controls::Image>();
        if (!image) return;
        m_attempted.clear();
        m_exhausted = false;
        image.Opacity(0);
        ShowPlaceholder(true);
        TryNextSource();
    }

    void PosterCard::TryNextSource()
    {
        auto const image = FindName(L"PosterImage").try_as<Microsoft::UI::Xaml::Controls::Image>();
        if (!image) return;
        // At most two sources, so a url the bitmap refuses outright moves on in a
        // bounded loop rather than waiting for a failure event.
        for (auto const& candidate : ::HaloDesktop::Controls::ArtworkCandidates(m_poster, L"", m_metaId))
        {
            if (std::find(m_attempted.begin(), m_attempted.end(), candidate) != m_attempted.end()) continue;
            m_attempted.push_back(candidate);
            try
            {
                Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
                // Set before the uri, or the decode has already been scheduled at full
                // size. Poster sources run to a thousand pixels wide for a slot a sixth
                // of that, and every realised card was holding the whole thing.
                bitmap.DecodePixelWidth(::HaloDesktop::Shell::PosterDecodeWidth());
                bitmap.UriSource(winrt::Windows::Foundation::Uri{ candidate });
                image.Source(bitmap);
                return;
            }
            catch (...)
            {
                // A url the bitmap refuses outright: on to the next.
            }
        }
        image.Source(nullptr);
        image.Opacity(0);
        if (!m_exhausted)
        {
            m_exhausted = true;
            RaisePosterFailed();
        }
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
    winrt::event_token PosterCard::PosterFailed(Microsoft::UI::Xaml::RoutedEventHandler const& handler) { return m_posterFailed.add(handler); }
    void PosterCard::PosterFailed(winrt::event_token const& token) noexcept { m_posterFailed.remove(token); }
    // Raised after the current turn, never from inside a setter: a recycled card
    // is handed its new item one binding at a time, so mid-way its Tag and its
    // art can name different titles, and a handler that changes the list would
    // be changing it while the list builds its rows. The card raises only if it
    // is still out of art for the same poster and id once the bindings settled.
    void PosterCard::RaisePosterFailed()
    {
        if (m_poster.empty()) return;
        auto const raise = [weak = get_weak(), poster = m_poster, metaId = m_metaId]()
        {
            auto const self = weak.get();
            if (!self || !self->m_exhausted || self->m_poster != poster || self->m_metaId != metaId) return;
            try
            {
                self->m_posterFailed(*self, Microsoft::UI::Xaml::RoutedEventArgs{});
            }
            catch (winrt::hresult_error const&)
            {
                // A handler on a page being torn down: the notice is only a hint.
            }
        };
        try
        {
            if (DispatcherQueue().TryEnqueue(raise)) return;
        }
        catch (winrt::hresult_error const&)
        {
        }
        // No queue to defer to: the card is going away and the notice with it.
    }
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
            m_hoverShadow.Prepare(ShadowHost(), *this);
        }
        Translation({ 0.0F, hovered ? ::HaloDesktop::Controls::CardHoverLift : 0.0F, 0.0F });
        if (auto const ring = ArtHoverRing()) ring.Opacity(hovered ? 1.0 : 0.0);
        if (auto const rest = ArtRestRing()) rest.Opacity(hovered ? 0.0 : 1.0);
        if (auto const fade = ShadowFade()) fade.Opacity(hovered ? 1.0 : 0.0);
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
        ShowPlaceholder(true);
        TryNextSource();
    }
}
