#include "pch.h"
#include "Views/DetailSimilarCard.xaml.h"
#if __has_include("DetailSimilarCard.g.cpp")
#include "DetailSimilarCard.g.cpp"
#endif

#include "Controls/ThemeColorLookup.h"

namespace winrt::HaloDesktop::implementation
{
    DetailSimilarCard::DetailSimilarCard()
    {
        m_hoverGate.Attach(*this, [this](bool hovered) { SetHovered(hovered); });
    }

    void DetailSimilarCard::Media(winrt::HaloDesktop::MediaSummary const& value)
    {
        if (m_media == value) return;
        m_media = value;
        if (Bindings) Bindings->Update();
        // `★ 6.8`, only for a title that has a rating (MediaSummary.Rating is
        // already the rated value, empty when there is none).
        auto const rating = m_media ? m_media.Rating() : winrt::hstring{};
        RatingText().Text(rating.empty() ? winrt::hstring{} : L"\x2605 " + rating);
        RatingBadge().Visibility(rating.empty() ? Microsoft::UI::Xaml::Visibility::Collapsed : Microsoft::UI::Xaml::Visibility::Visible);
        YearText().Visibility(m_media && !m_media.ReleaseInfo().empty() ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed);
    }

    void DetailSimilarCard::OnClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_click(*this, args);
    }

    void DetailSimilarCard::OnHoverStarted(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        m_hoverGate.PointerEntered(sender.as<Microsoft::UI::Xaml::FrameworkElement>());
    }

    void DetailSimilarCard::OnHoverEnded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        m_hoverGate.PointerExited();
    }

    void DetailSimilarCard::SetHovered(bool hovered)
    {
        if (auto const art = ArtFrame()) art.Translation({ 0.0F, hovered ? -3.0F : 0.0F, 0.0F });
        if (auto const badge = RatingBadge()) badge.Opacity(hovered ? 1.0 : 0.0);
        // `.similar:hover .similar-name`: the name brightens to full ink.
        if (auto const name = NameText())
        {
            // Read for the card's own theme: the application's resources
            // answer for the theme the app started in.
            if (auto const color = ::HaloDesktop::Controls::ThemeColor(name, hovered ? L"HaloDetailInkColor" : L"HaloDetailInk78Color"))
            {
                name.Foreground(Microsoft::UI::Xaml::Media::SolidColorBrush{ *color });
            }
        }
    }
}
