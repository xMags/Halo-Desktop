#include "pch.h"
#include "Views/HomePage.xaml.h"
#if __has_include("HomePage.g.cpp")
#include "HomePage.g.cpp"
#endif

#include "App.xaml.h"
#include "Services/ServiceInterfaces.h"

#include <winrt/Windows.UI.Text.h>

namespace winrt::HaloDesktop::implementation
{
    HomePage::HomePage() = default;

    void HomePage::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const list = ShelfProofList();
        list.Children().Clear();
        for (auto const& shelf : App::Services().Catalog->Shelves())
        {
            Microsoft::UI::Xaml::Controls::TextBlock title;
            title.Text(shelf.Title());
            title.FontSize(18);
            title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            list.Children().Append(title);
        }
    }
}
