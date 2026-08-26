#include "pch.h"
#include "Controls/PosterCard.xaml.h"
#if __has_include("PosterCard.g.cpp")
#include "PosterCard.g.cpp"
#endif

#include "App.xaml.h"
#include "Shell/LayoutMetricsService.h"

#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace winrt::HaloDesktop::implementation
{
    PosterCard::PosterCard() = default;
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
        if (value.empty()) { image.Source(nullptr); return; }
        try
        {
            Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
            // Set before the uri, or the decode has already been scheduled at full
            // size. Poster sources run to a thousand pixels wide for a slot a sixth
            // of that, and every realised card was holding the whole thing.
            bitmap.DecodePixelType(Microsoft::UI::Xaml::Media::Imaging::DecodePixelType::Logical);
            bitmap.DecodePixelWidth(static_cast<std::int32_t>(::HaloDesktop::Shell::LargestPosterWidth()));
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
        Width(current.PosterWidth);
        if (auto const row = FindName(L"ArtRow").try_as<Microsoft::UI::Xaml::Controls::RowDefinition>())
        {
            row.Height(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(current.PosterArtHeight()));
        }
    }

    winrt::event_token PosterCard::Click(Microsoft::UI::Xaml::RoutedEventHandler const& handler) { return m_click.add(handler); }
    void PosterCard::Click(winrt::event_token const& token) noexcept { m_click.remove(token); }
    void PosterCard::OnCardClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args) { m_click(*this, args); }
    void PosterCard::OnCardPointerEntered([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        Translation({ 0.0F, -2.0F, 0.0F });
        ArtBorderControl().BorderBrush(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(winrt::box_value(L"HaloAccentBrush")).as<Microsoft::UI::Xaml::Media::Brush>());
    }
    void PosterCard::OnCardPointerExited([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        Translation({ 0.0F, 0.0F, 0.0F });
        ArtBorderControl().BorderBrush(Microsoft::UI::Xaml::Application::Current().Resources().Lookup(winrt::box_value(L"HaloCardStrokeBrush")).as<Microsoft::UI::Xaml::Media::Brush>());
    }
    Microsoft::UI::Xaml::Controls::Border PosterCard::ArtBorderControl() const { return FindName(L"ArtBorder").as<Microsoft::UI::Xaml::Controls::Border>(); }
    void PosterCard::OnPosterOpened(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto image = FindName(L"PosterImage").try_as<Microsoft::UI::Xaml::Controls::Image>()) image.Opacity(1);
    }
    void PosterCard::OnPosterFailed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::ExceptionRoutedEventArgs const&)
    {
        if (auto image = FindName(L"PosterImage").try_as<Microsoft::UI::Xaml::Controls::Image>()) image.Opacity(0);
    }
}
