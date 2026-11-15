#include "pch.h"
#include "Views/DetailCreditCard.xaml.h"
#if __has_include("DetailCreditCard.g.cpp")
#include "DetailCreditCard.g.cpp"
#endif

#include <chrono>
#include <winrt/Windows.UI.Text.h>

namespace winrt::HaloDesktop::implementation
{
    void DetailCreditCard::InitializeComponent()
    {
        DetailCreditCardT::InitializeComponent();
        // `.credit img { transition: transform 0.25s }`, about the photo's centre.
        Microsoft::UI::Xaml::Vector3Transition zoom;
        zoom.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::milliseconds{ 250 }));
        Photo().ScaleTransition(zoom);
        Photo().CenterPoint({ 64.0F, 80.0F, 0.0F });
    }

    void DetailCreditCard::Credit(winrt::HaloDesktop::DetailCreditViewModel const& value)
    {
        if (m_credit == value) return;
        m_credit = value;
        if (Bindings) Bindings->Update();
    }

    void DetailCreditCard::OnClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        m_click(*this, args);
    }

    void DetailCreditCard::OnHoverStarted(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        SetHovered(true);
    }

    void DetailCreditCard::OnHoverEnded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
    {
        SetHovered(false);
    }

    // `.credit:hover`: the photo zooms to 1.04, the badge drops in and the name
    // is underlined.
    void DetailCreditCard::SetHovered(bool hovered)
    {
        auto const scale = hovered ? 1.04F : 1.0F;
        if (auto const photo = Photo()) photo.Scale({ scale, scale, 1.0F });
        if (auto const badge = OpenBadge()) badge.Opacity(hovered ? 1.0 : 0.0);
        if (auto const name = NameText())
        {
            name.TextDecorations(hovered ? winrt::Windows::UI::Text::TextDecorations::Underline : winrt::Windows::UI::Text::TextDecorations::None);
        }
    }
}
