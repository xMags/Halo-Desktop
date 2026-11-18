#include "pch.h"
#include "Views/DownloadsPage.xaml.h"
#if __has_include("DownloadsPage.g.cpp")
#include "DownloadsPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Views/DownloadReadyCard.xaml.h"
#include "Views/PageDialog.h"
#include "ViewModels/DownloadsViewModel.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.Windows.Storage.Pickers.h>

namespace
{
    // `.dl-pane`: 380px, 320px on a content surface narrower than 1200px.
    constexpr double PaneWide = 380.0;
    constexpr double PaneNarrow = 320.0;
    constexpr double PaneBreakpoint = 1200.0;
    // `@container dl-main (max-width: 840px)`: storage goes under the chart.
    constexpr double InstrumentBreakpoint = 840.0;
    // `.dl-pane-inner { padding: 28px 24px 24px }`.
    constexpr double PaneHorizontalPadding = 48.0;
    // `.dl-instrument { gap: 28px }` either side of the 1px rule.
    constexpr double InstrumentRuleWidth = 57.0;
    constexpr double StorageWidth = 240.0;

    winrt::hstring TagOf(winrt::Windows::Foundation::IInspectable const& sender)
    {
        auto const element = sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
        return element ? winrt::unbox_value_or<winrt::hstring>(element.Tag(), L"") : winrt::hstring{};
    }

    winrt::hstring CardIdOf(winrt::Windows::Foundation::IInspectable const& sender)
    {
        auto const card = sender.try_as<winrt::HaloDesktop::DownloadReadyCard>();
        auto const row = card ? card.Row() : nullptr;
        return row ? row.Id() : winrt::hstring{};
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
        TransferList().ItemsSource(viewModel->TransfersView());
        ReadyGrid().ItemsSource(viewModel->ReadyView());
        m_chart.Attach(ChartHost(), ChartBrushSource());
        if (m_viewModelChangedToken.value == 0)
        {
            m_viewModelChangedToken = m_viewModel.PropertyChanged([weak = get_weak()](
                winrt::Windows::Foundation::IInspectable const&,
                Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args)
            {
                if (args.PropertyName() != L"ChartBars") return;
                if (auto const self = weak.get()) self->ApplyChart();
            });
        }
        ApplyChart();
        SyncFilterSelection();
    }
    void DownloadsPage::OnUnloaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_viewModelChangedToken.value != 0)
        {
            m_viewModel.PropertyChanged(m_viewModelChangedToken);
            m_viewModelChangedToken = {};
        }
        winrt::get_self<DownloadsViewModel>(m_viewModel)->Deactivate();
    }
    void DownloadsPage::ApplyChart()
    {
        m_chart.Apply(winrt::get_self<DownloadsViewModel>(m_viewModel)->ChartBars());
    }
    // The page is cached, so a return visit reuses the markup's initial checked
    // state while the view model still holds the filter the viewer left on.
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

    // Layout only; a refusal leaves the last layout in place.
    void DownloadsPage::OnPageSizeChanged([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        try
        {
            auto const width = args.NewSize().Width < PaneBreakpoint ? PaneNarrow : PaneWide;
            if (auto const column = PaneColumn(); column && column.Width().Value != width)
            {
                column.Width(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(width));
            }
        }
        catch (winrt::hresult_error const&)
        {
        }
    }
    void DownloadsPage::OnMainSizeChanged([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        try
        {
            SetInstrumentStacked(args.NewSize().Width <= InstrumentBreakpoint);
        }
        catch (winrt::hresult_error const&)
        {
        }
    }
    void DownloadsPage::SetInstrumentStacked(bool stacked)
    {
        if (stacked == m_instrumentStacked) return;
        m_instrumentStacked = stacked;
        auto const storage = StorageBlock();
        Microsoft::UI::Xaml::Controls::Grid::SetRow(storage, stacked ? 1 : 0);
        Microsoft::UI::Xaml::Controls::Grid::SetColumn(storage, stacked ? 0 : 2);
        storage.Margin(Microsoft::UI::Xaml::ThicknessHelper::FromLengths(0, stacked ? 18 : 0, 0, 0));
        InstrumentRule().Visibility(stacked ? Microsoft::UI::Xaml::Visibility::Collapsed : Microsoft::UI::Xaml::Visibility::Visible);
        InstrumentRuleColumn().Width(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(stacked ? 0.0 : InstrumentRuleWidth));
        InstrumentStorageColumn().Width(Microsoft::UI::Xaml::GridLengthHelper::FromPixels(stacked ? 0.0 : StorageWidth));
    }
    // The pane's content is at least the pane's height, so the delete button
    // sits at its foot, and its art keeps 16:9 at the pane's width.
    void DownloadsPage::OnPaneSizeChanged([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        try
        {
            auto const size = args.NewSize();
            if (auto const content = PaneContent()) content.MinHeight(size.Height);
            auto const artWidth = size.Width - PaneHorizontalPadding;
            if (artWidth > 0.0)
            {
                if (auto const art = PaneArt()) art.Height(std::round(artWidth * 9.0 / 16.0));
            }
        }
        catch (winrt::hresult_error const&)
        {
        }
    }

    void DownloadsPage::OnTransferItemClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        if (auto const row = args.ClickedItem().try_as<winrt::HaloDesktop::DownloadRowViewModel>()) m_viewModel.Select(row.Id());
    }
    void DownloadsPage::OnCardSelected(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto const id = CardIdOf(sender); !id.empty()) m_viewModel.Select(id);
    }
    void DownloadsPage::OnCardPlayRequested(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const id = CardIdOf(sender);
        if (id.empty()) return;
        m_viewModel.Select(id);
        m_viewModel.Play(id);
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
    void DownloadsPage::OnRevealSelectedClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.RevealSelected(); }
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
                L"The partial file and protected request will be removed from this device.",
                ::HaloDesktop::Views::DialogTone::Destructive);
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
            auto dialog = ::HaloDesktop::Views::MakeDeleteFromDeviceDialog(XamlRoot(), ActualTheme());
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
            HWND windowHandle{};
            winrt::check_hresult(App::Window().as<::IWindowNative>()->get_WindowHandle(&windowHandle));
            winrt::Microsoft::Windows::Storage::Pickers::FolderPicker picker{
                winrt::Microsoft::UI::GetWindowIdFromWindow(windowHandle) };
            // Open on the folder downloads go to now. Without a suggested
            // folder Windows opens the picker wherever it was last, Documents
            // at first, which reads as the current download location. The
            // picker skips a suggested folder that no longer exists by itself;
            // only an empty path is refused by the property, so it is not set.
            auto const current = winrt::get_self<DownloadsViewModel>(m_viewModel)->CurrentDownloadDirectory();
            if (!current.empty())
            {
                picker.SuggestedFolder(current.wstring());
            }
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
