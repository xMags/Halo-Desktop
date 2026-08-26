#include "pch.h"
#include "Controls/ContinueCard.xaml.h"
#if __has_include("ContinueCard.g.cpp")
#include "ContinueCard.g.cpp"
#endif

#include "App.xaml.h"
#include "Controls/ArtworkImage.xaml.h"
#include "Controls/TagChip.xaml.h"
#include "Shell/LayoutMetricsService.h"

namespace winrt::HaloDesktop::implementation
{
    ContinueCard::ContinueCard() = default;

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

    winrt::hstring ContinueCard::Poster() const { return m_poster; }
    void ContinueCard::Poster(winrt::hstring const& value)
    {
        m_poster = value;
        auto still = FindName(L"Still").try_as<winrt::HaloDesktop::ArtworkImage>();
        if (!still) return;
        still.DecodeWidth(::HaloDesktop::Shell::LargestContinueWidth());
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
    void ContinueCard::Progress(double value)
    {
        m_progress = value;
        if (auto bar = FindName(L"ProgressTrack").try_as<Microsoft::UI::Xaml::Controls::ProgressBar>()) bar.Value(value);
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
        m_cardWidth = current.ContinueWidth;
        Width(current.ContinueWidth);
        if (auto const row = FindName(L"ArtRow").try_as<Microsoft::UI::Xaml::Controls::RowDefinition>())
        {
            row.Height(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(current.ContinueArtHeight()));
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
}
