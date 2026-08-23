#pragma once

#include "ConnectPage.g.h"

#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct ConnectPage : ConnectPageT<ConnectPage>
    {
        ConnectPage();

        [[nodiscard]] winrt::HaloDesktop::ConnectViewModel ViewModel() const;
        void OnTestServerClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnContinueClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        winrt::HaloDesktop::ConnectViewModel m_viewModel{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct ConnectPage : ConnectPageT<ConnectPage, implementation::ConnectPage> {};
}
