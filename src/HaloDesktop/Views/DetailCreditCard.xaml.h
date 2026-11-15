#pragma once

#include "DetailCreditCard.g.h"

#include <winrt/Microsoft.UI.Xaml.Input.h>

namespace winrt::HaloDesktop::implementation
{
    // A person on the title page's cast row. View behaviour only.
    struct DetailCreditCard : DetailCreditCardT<DetailCreditCard>
    {
        DetailCreditCard() = default;
        void InitializeComponent();

        [[nodiscard]] winrt::HaloDesktop::DetailCreditViewModel Credit() const { return m_credit; }
        void Credit(winrt::HaloDesktop::DetailCreditViewModel const& value);

        winrt::event_token Click(Microsoft::UI::Xaml::RoutedEventHandler const& handler) { return m_click.add(handler); }
        void Click(winrt::event_token const& token) noexcept { m_click.remove(token); }

        void OnClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnHoverStarted(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnHoverEnded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);

    private:
        void SetHovered(bool hovered);

        winrt::HaloDesktop::DetailCreditViewModel m_credit{ nullptr };
        winrt::event<Microsoft::UI::Xaml::RoutedEventHandler> m_click;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct DetailCreditCard : DetailCreditCardT<DetailCreditCard, implementation::DetailCreditCard>
    {
    };
}
