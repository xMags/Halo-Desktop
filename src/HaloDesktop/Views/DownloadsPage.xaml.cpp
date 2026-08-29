#include "pch.h"
#include "Views/DownloadsPage.xaml.h"
#if __has_include("DownloadsPage.g.cpp")
#include "DownloadsPage.g.cpp"
#endif

#include "App.xaml.h"
#include "ViewModels/DownloadsViewModel.h"

#include <microsoft.ui.xaml.window.h>
#include <shobjidl_core.h>
#include <winrt/Windows.Storage.Pickers.h>

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
    void DownloadsPage::OnCancelSelectedClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ShowCancelDialog(); }
    void DownloadsPage::OnDeleteSelectedClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ShowDeleteDialog(); }
    void DownloadsPage::OnPlaySelectedClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenPlayer(); }
    void DownloadsPage::OnChooseSourceClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.ChooseSource(); }
    void DownloadsPage::OnManageFolderClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { PickDownloadFolder(); }

    winrt::fire_and_forget DownloadsPage::ShowPauseAllDialog()
    {
        auto lifetime = get_strong();
        try
        {
            Microsoft::UI::Xaml::Controls::ContentDialog dialog;
            dialog.XamlRoot(XamlRoot());
            dialog.Title(winrt::box_value(L"Pause all transfers?"));
            dialog.Content(winrt::box_value(L"Every active and queued transfer will stay paused until you resume all."));
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
    winrt::fire_and_forget DownloadsPage::ShowCancelDialog()
    {
        auto lifetime = get_strong();
        try
        {
            Microsoft::UI::Xaml::Controls::ContentDialog dialog;
            dialog.XamlRoot(XamlRoot());
            dialog.Title(winrt::box_value(L"Cancel transfer?"));
            dialog.Content(winrt::box_value(L"The partial file and protected request will be removed from this device."));
            dialog.PrimaryButtonText(L"Cancel transfer");
            dialog.CloseButtonText(L"Keep transfer");
            if (co_await dialog.ShowAsync() == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
            {
                m_viewModel.CancelSelected();
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
            Microsoft::UI::Xaml::Controls::ContentDialog dialog;
            dialog.XamlRoot(XamlRoot());
            dialog.Title(winrt::box_value(L"Delete from device?"));
            dialog.Content(winrt::box_value(L"This permanently removes the video and its subtitle sidecar from this device."));
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
                Microsoft::UI::Xaml::Controls::ContentDialog dialog;
                dialog.XamlRoot(XamlRoot());
                dialog.Title(winrt::box_value(L"Folder could not be changed"));
                dialog.Content(winrt::box_value(L"Choose an available local folder and try again."));
                dialog.CloseButtonText(L"Close");
                co_await dialog.ShowAsync();
            }
            catch (...)
            {
            }
        }
    }
}
