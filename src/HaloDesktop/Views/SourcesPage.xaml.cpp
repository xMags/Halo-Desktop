#include "pch.h"
#include "Views/SourcesPage.xaml.h"
#if __has_include("SourcesPage.g.cpp")
#include "SourcesPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Services/NavigationService.h"
#include "ViewModels/SourcesViewModel.h"

namespace winrt::HaloDesktop::implementation
{
    SourcesPage::SourcesPage()
        : m_viewModel(winrt::make<SourcesViewModel>(App::Services()))
    {
    }
    winrt::HaloDesktop::SourcesViewModel SourcesPage::ViewModel() const { return m_viewModel; }
    void SourcesPage::OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args) { m_viewModel.Load(args.Parameter()); }
    void SourcesPage::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const viewModel = winrt::get_self<SourcesViewModel>(m_viewModel);
        viewModel->Activate();
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
    void SourcesPage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        winrt::get_self<SourcesViewModel>(m_viewModel)->Deactivate();
    }
    void SourcesPage::OnAllFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(0); }
    void SourcesPage::OnInstantFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(1); }
    void SourcesPage::On2160FilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(2); }
    void SourcesPage::On1080FilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(3); }
    void SourcesPage::OnRetryClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Retry(); }
    void SourcesPage::OnPlayClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenBest(); }
    void SourcesPage::OnDownloadBestClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        StartDownload(winrt::get_self<SourcesViewModel>(m_viewModel)->BestKey());
    }
    void SourcesPage::OnDownloadSourceClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        StartDownload(winrt::unbox_value_or<winrt::hstring>(
            sender.as<Microsoft::UI::Xaml::Controls::Button>().Tag(), L""));
    }
    void SourcesPage::OnSourceRowClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenPlayer(winrt::unbox_value_or<winrt::hstring>(sender.as<Microsoft::UI::Xaml::Controls::Button>().Tag(),L"")); }
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

    winrt::fire_and_forget SourcesPage::StartDownload(winrt::hstring key)
    {
        auto lifetime = get_strong();
        if (key.empty())
        {
            co_return;
        }
        auto const viewModel = winrt::get_self<SourcesViewModel>(m_viewModel);
        auto outcome = co_await viewModel->StartDownloadAsync(key, false);
        if (outcome == ::HaloDesktop::Services::DownloadStartOutcome::ReplacementRequired)
        {
            if (!co_await ConfirmReplacementAsync())
            {
                co_return;
            }
            outcome = co_await viewModel->StartDownloadAsync(key, true);
        }
        if (outcome == ::HaloDesktop::Services::DownloadStartOutcome::Started
            || outcome == ::HaloDesktop::Services::DownloadStartOutcome::AlreadyExists)
        {
            App::Services().Navigation->GoTo(::HaloDesktop::Services::Page::Downloads);
            co_return;
        }
        co_await ShowDownloadFailureAsync();
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> SourcesPage::ConfirmReplacementAsync()
    {
        Microsoft::UI::Xaml::Controls::ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(winrt::box_value(L"Replace the saved source?"));
        dialog.Content(winrt::box_value(
            L"Halo will keep the current file until the replacement finishes successfully."));
        dialog.PrimaryButtonText(L"Replace");
        dialog.CloseButtonText(L"Keep current");
        dialog.DefaultButton(Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
        co_return co_await dialog.ShowAsync()
            == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary;
    }

    winrt::Windows::Foundation::IAsyncAction SourcesPage::ShowDownloadFailureAsync()
    {
        Microsoft::UI::Xaml::Controls::ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(winrt::box_value(L"Download could not start"));
        dialog.Content(winrt::box_value(
            L"Check the source, free storage, and download folder, then try again."));
        dialog.CloseButtonText(L"Close");
        co_await dialog.ShowAsync();
    }
}
