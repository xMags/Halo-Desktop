#include "pch.h"
#include "Controls/PosterCard.xaml.h"
#if __has_include("PosterCard.g.cpp")
#include "PosterCard.g.cpp"
#endif

#include <winrt/Windows.Foundation.Numerics.h>

namespace winrt::HaloDesktop::implementation
{
    PosterCard::PosterCard() = default;
    winrt::hstring PosterCard::Title() const { return m_title; }
    void PosterCard::Title(winrt::hstring const& value) { m_title = value; if (auto text = FindName(L"TitleText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>()) text.Text(value); }
    winrt::hstring PosterCard::Meta() const { return m_meta; }
    void PosterCard::Meta(winrt::hstring const& value) { m_meta = value; if (auto text = FindName(L"MetaText").try_as<Microsoft::UI::Xaml::Controls::TextBlock>()) text.Text(value); }
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
}
