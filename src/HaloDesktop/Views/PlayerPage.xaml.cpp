#include "pch.h"
#include "Views/PlayerPage.xaml.h"
#if __has_include("PlayerPage.g.cpp")
#include "PlayerPage.g.cpp"
#endif
#include "App.xaml.h"
#include "Services/NavigationService.h"

namespace winrt::HaloDesktop::implementation
{
    PlayerPage::PlayerPage() = default;
    void PlayerPage::OnBackClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        App::Services().Navigation->CloseOverlay();
    }
}
