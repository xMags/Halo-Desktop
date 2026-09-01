#include "pch.h"
#include "Views/DownloadsPage.xaml.h"
#if __has_include("DownloadsPage.g.cpp")
#include "DownloadsPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Views/PageDialog.h"
#include "ViewModels/DownloadsViewModel.h"

#include <array>
#include <cstdint>
#include <microsoft.ui.xaml.window.h>
#include <shobjidl_core.h>
#include <winrt/Windows.Storage.Pickers.h>

namespace
{
    winrt::hstring TagOf(winrt::Windows::Foundation::IInspectable const& sender)
    {
        auto const element = sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
        return element ? winrt::unbox_value_or<winrt::hstring>(element.Tag(), L"") : winrt::hstring{};
    }
}

namespace winrt::HaloDesktop::implementation
{
    DownloadsPage::DownloadsPage()
        : m_viewModel(winrt::make<DownloadsViewModel>(App::Services()))
    {
    }
    winrt::HaloDesktop::DownloadsViewModel DownloadsPage::ViewModel() const { return m_viewModel; }
    void DownloadsPage::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const viewModel = winrt::get_self<DownloadsViewModel>(m_viewModel);
        viewModel->Activate();
        FindName(L"TransferList").as<Microsoft::UI::Xaml::Controls::ListView>().ItemsSource(viewModel->TransfersView());
        auto const readyList = FindName(L"ReadyList").as<Microsoft::UI::Xaml::Controls::ListView>();
        readyList.ItemsSource(viewModel->ReadyView());
        FindName(L"ChartList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->ChartBarsView());
        readyList.SelectedItem(viewModel->SelectedRow());
        SyncFilterSelection();
    }
    // The page is cached, so a return visit reuses the markup's initial checked
    // state while the view model still holds the filter the viewer left on. The
    // selector is pushed back into agreement rather than the filter being reset.
    void DownloadsPage::SyncFilterSelection()
    {
        std::array<wchar_t const*, 4> const names{ L"AllFilter", L"ActiveFilter", L"ReadyFilter", L"FailedFilter" };
        auto const index = m_viewModel.FilterIndex();
        for (std::size_t position = 0; position < names.size(); ++position)
        {
            if (auto const button = FindName(names[position]).try_as<Microsoft::UI::Xaml::Controls::RadioButton>())
            {
                button.IsChecked(static_cast<std::int32_t>(position) == index);
            }
        }
    }
    void DownloadsPage::OnUnloaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        winrt::get_self<DownloadsViewModel>(m_viewModel)->Deactivate();
    }
    void DownloadsPage::OnTransferItemClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        FindName(L"ReadyList").as<Microsoft::UI::Xaml::Controls::ListView>().SelectedItem(nullptr);
        m_viewModel.Select(args.ClickedItem().as<winrt::HaloDesktop::DownloadRowViewModel>().Id());
    }
    void DownloadsPage::OnReadyItemClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        FindName(L"TransferList").as<Microsoft::UI::Xaml::Controls::ListView>().SelectedItem(nullptr);
        m_viewModel.Select(args.ClickedItem().as<winrt::HaloDesktop::DownloadRowViewModel>().Id());
    }
    void DownloadsPage::OnAllFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(0); }
    void DownloadsPage::OnActiveFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(1); }
    void DownloadsPage::OnReadyFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(2); }
    void DownloadsPage::OnFailedFilterClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(3); }
    void DownloadsPage::OnPauseAllClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_viewModel.IsPausedAll())
        {
            m_viewModel.ResumeAll();
            return;
        }
        ShowPauseAllDialog();
    }
    void DownloadsPage::OnPauseSelectedClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.PauseSelected(); }
    void DownloadsPage::OnResumeSelectedClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.ResumeSelected(); }
    void DownloadsPage::OnCancelSelectedClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ShowCancelDialog(L""); }
    void DownloadsPage::OnDeleteSelectedClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ShowDeleteDialog(); }
    void DownloadsPage::OnPlaySelectedClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenPlayer(); }
    void DownloadsPage::OnChooseSourceClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.ChooseSource(); }
    void DownloadsPage::OnPauseRowClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Pause(TagOf(sender)); }
    void DownloadsPage::OnResumeRowClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Resume(TagOf(sender)); }
    void DownloadsPage::OnRetryRowClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Retry(TagOf(sender)); }
    void DownloadsPage::OnChooseSourceRowClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.ChooseSourceFor(TagOf(sender)); }
    void DownloadsPage::OnCancelRowClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ShowCancelDialog(TagOf(sender)); }
    void DownloadsPage::OnManageFolderClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { PickDownloadFolder(); }
    void DownloadsPage::OnOpenFolderClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenDownloadFolder(); }
    void DownloadsPage::OnRetryFailedClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.RetryFailed(); }
    void DownloadsPage::OnBrowseLibraryClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.BrowseLibrary(); }

    winrt::fire_and_forget DownloadsPage::ShowPauseAllDialog()
    {
        auto lifetime = get_strong();
        try
        {
            auto dialog = ::HaloDesktop::Views::MakeDialog(
                XamlRoot(),
                ActualTheme(),
                L"Pause all transfers?",
                L"Every active and queued transfer will stay paused until you resume all.");
            dialog.PrimaryButtonText(L"Pause all");
            dialog.CloseButtonText(L"Cancel");
            dialog.DefaultButton(Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
            if (co_await dialog.ShowAsync() == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
            {
                m_viewModel.PauseAll();
            }
        }
        catch (...)
        {
        }
    }
    winrt::fire_and_forget DownloadsPage::ShowCancelDialog(winrt::hstring id)
    {
        auto lifetime = get_strong();
        try
        {
            auto dialog = ::HaloDesktop::Views::MakeDialog(
                XamlRoot(),
                ActualTheme(),
                L"Cancel transfer?",
                L"The partial file and protected request will be removed from this device.");
            dialog.PrimaryButtonText(L"Cancel transfer");
            dialog.CloseButtonText(L"Keep transfer");
            if (co_await dialog.ShowAsync() == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
            {
                if (id.empty()) m_viewModel.CancelSelected();
                else m_viewModel.Cancel(id);
            }
        }
        catch (...)
        {
        }
    }
    winrt::fire_and_forget DownloadsPage::ShowDeleteDialog()
    {
        auto lifetime = get_strong();
        try
        {
            auto dialog = ::HaloDesktop::Views::MakeDialog(
                XamlRoot(),
                ActualTheme(),
                L"Delete from device?",
                L"This permanently removes the video and its subtitle sidecar from this device.");
            dialog.PrimaryButtonText(L"Delete");
            dialog.CloseButtonText(L"Cancel");
            if (co_await dialog.ShowAsync() == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
            {
                m_viewModel.DeleteSelected();
            }
        }
        catch (...)
        {
        }
    }

    winrt::fire_and_forget DownloadsPage::PickDownloadFolder()
    {
        auto lifetime = get_strong();
        bool failed{};
        try
        {
            winrt::Windows::Storage::Pickers::FolderPicker picker;
            picker.FileTypeFilter().Append(L"*");
            HWND windowHandle{};
            winrt::check_hresult(App::Window().as<::IWindowNative>()->get_WindowHandle(&windowHandle));
            winrt::check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(windowHandle));
            auto const folder = co_await picker.PickSingleFolderAsync();
            if (folder)
            {
                co_await winrt::get_self<DownloadsViewModel>(m_viewModel)->SetDownloadDirectoryAsync(
                    std::filesystem::path{ folder.Path().c_str() });
            }
        }
        catch (...)
        {
            failed = true;
        }
        if (failed)
        {
            try
            {
                auto dialog = ::HaloDesktop::Views::MakeDialog(
                    XamlRoot(),
                    ActualTheme(),
                    L"Folder could not be changed",
                    L"Choose an available local folder and try again.");
                dialog.CloseButtonText(L"Close");
                co_await dialog.ShowAsync();
            }
            catch (...)
            {
            }
        }
    }
}
