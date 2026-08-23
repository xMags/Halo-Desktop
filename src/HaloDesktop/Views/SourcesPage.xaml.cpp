#include "pch.h"
#include "Views/SourcesPage.xaml.h"
#if __has_include("SourcesPage.g.cpp")
#include "SourcesPage.g.cpp"
#endif

#include "App.xaml.h"
#include "ViewModels/SourcesViewModel.h"

namespace winrt::HaloDesktop::implementation
{
    SourcesPage::SourcesPage()
        : m_viewModel(winrt::make<SourcesViewModel>(App::Services()))
    {
    }
    winrt::HaloDesktop::SourcesViewModel SourcesPage::ViewModel() const { return m_viewModel; }
    void SourcesPage::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const viewModel = winrt::get_self<SourcesViewModel>(m_viewModel);
        FindName(L"SourceList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->ItemsView());
        auto const tip = FindName(L"RankingTip").as<Microsoft::UI::Xaml::Controls::TeachingTip>();
        tip.Target(FindName(L"BestSourceCard").as<Microsoft::UI::Xaml::FrameworkElement>());
        if (m_viewModel.TeachingTipOpen())
        {
            DispatcherQueue().TryEnqueue([weak = get_weak()]()
            {
                if (auto const self = weak.get())
                {
                    self->FindName(L"RankingTip")
                        .as<Microsoft::UI::Xaml::Controls::TeachingTip>()
                        .IsOpen(true);
                }
            });
        }
    }
    void SourcesPage::OnAllFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(0); }
    void SourcesPage::OnInstantFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(1); }
    void SourcesPage::On2160FilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(2); }
    void SourcesPage::On1080FilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(3); }
    void SourcesPage::OnPlayClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenPlayer(); }
    void SourcesPage::OnSourceRowClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenPlayer(); }
    void SourcesPage::OnSourceRowPointerEntered(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        sender.as<Microsoft::UI::Xaml::Controls::Button>().BorderBrush(
            Microsoft::UI::Xaml::Application::Current().Resources()
                .Lookup(winrt::box_value(L"HaloAccentBrush"))
                .as<Microsoft::UI::Xaml::Media::Brush>());
    }
    void SourcesPage::OnSourceRowPointerExited(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        sender.as<Microsoft::UI::Xaml::Controls::Button>().BorderBrush(
            Microsoft::UI::Xaml::Application::Current().Resources()
                .Lookup(winrt::box_value(L"HaloCardStrokeBrush"))
                .as<Microsoft::UI::Xaml::Media::Brush>());
    }
    void SourcesPage::OnEditPlaybackClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenSettings(); }
    void SourcesPage::OnTeachingTipAction(Microsoft::UI::Xaml::Controls::TeachingTip const& sender, [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args)
    {
        m_viewModel.DismissTeachingTip();
        sender.IsOpen(false);
    }
    void SourcesPage::OnTeachingTipClosed(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::TeachingTip const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::TeachingTipClosedEventArgs const& args)
    {
        m_viewModel.DismissTeachingTip();
    }
}
