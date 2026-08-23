#pragma once
#include "PlayerPage.g.h"
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    struct PlayerPage : PlayerPageT<PlayerPage>
    {
        PlayerPage();
        void OnBackClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    };
}
namespace winrt::HaloDesktop::factory_implementation
{
    struct PlayerPage : PlayerPageT<PlayerPage, implementation::PlayerPage> {};
}
