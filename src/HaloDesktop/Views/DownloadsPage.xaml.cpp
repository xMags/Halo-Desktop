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
#include <cstdint>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.Windows.Storage.Pickers.h>

namespace
{
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

    winrt::Microsoft::UI::Xaml::Visibility VisibleWhen(bool visible) noexcept
    {
        return visible ? winrt::Microsoft::UI::Xaml::Visibility::Visible : winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
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
        RunningList().ItemsSource(viewModel->RunningView());
        WaitingList().ItemsSource(viewModel->WaitingView());
        AttentionList().ItemsSource(viewModel->AttentionView());
        ReadyGrid().ItemsSource(viewModel->ReadyView());
        m_chart.Attach(ChartHost(), ChartBrushSource());
        if (m_viewModelChangedToken.value == 0)
        {
            m_viewModelChangedToken = m_viewModel.PropertyChanged([weak = get_weak()](
                winrt::Windows::Foundation::IInspectable const&,
                Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args)
            {
                auto const self = weak.get();
                if (!self) return;
                auto const name = args.PropertyName();
                if (name == L"ChartBars") self->ApplyChart();
                else if (name == L"FilterAllCount") self->UpdateFilterTexts();
            });
        }
        ApplyChart();
        UpdateFilterTexts();
        SyncFilterSelection();
        ApplyFoldedCards();
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

    // The filter names carry their counts, as Windows' own selector bars do.
    // The view model raises every count together, so one of them stands for all.
    void DownloadsPage::UpdateFilterTexts()
    {
        try
        {
            AllFilterItem().Text(L"All (" + m_viewModel.FilterAllCount() + L")");
            ActiveFilterItem().Text(L"Active (" + m_viewModel.FilterActiveCount() + L")");
            ReadyFilterItem().Text(L"Ready (" + m_viewModel.FilterReadyCount() + L")");
            FailedFilterItem().Text(L"Failed (" + m_viewModel.FilterFailedCount() + L")");
        }
        catch (winrt::hresult_error const&)
        {
        }
    }

    // The page is cached, so a return visit reuses the markup's initial
    // selection while the view model still holds the filter the viewer left on.
    // Selecting it raises SelectionChanged, which sets the filter it already has.
    void DownloadsPage::SyncFilterSelection()
    {
        std::array const items{ AllFilterItem(), ActiveFilterItem(), ReadyFilterItem(), FailedFilterItem() };
        auto const index = m_viewModel.FilterIndex();
        if (index < 0 || static_cast<std::size_t>(index) >= items.size()) return;
        auto const& wanted = items[static_cast<std::size_t>(index)];
        if (FilterBar().SelectedItem() != wanted) FilterBar().SelectedItem(wanted);
    }
    void DownloadsPage::OnFilterSelectionChanged(Microsoft::UI::Xaml::Controls::SelectorBar const& sender, [[maybe_unused]] Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&)
    {
        auto const selected = sender.SelectedItem();
        std::array const items{ AllFilterItem(), ActiveFilterItem(), ReadyFilterItem(), FailedFilterItem() };
        for (std::size_t position = 0; position < items.size(); ++position)
        {
            if (items[position] == selected)
            {
                m_viewModel.SetFilter(static_cast<std::int32_t>(position));
                return;
            }
        }
    }

    void DownloadsPage::ApplyFoldedCards()
    {
        StatusDetails().Visibility(VisibleWhen(m_statusOpen));
        StatusChevronTurn().Angle(m_statusOpen ? 180.0 : 0.0);
        StatusHint().Text(m_statusOpen ? L"Hide details" : L"Details");
        WaitingList().Visibility(VisibleWhen(m_waitingOpen));
        WaitingChevronTurn().Angle(m_waitingOpen ? 180.0 : 0.0);
    }
    void DownloadsPage::OnStatusToggleClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_statusOpen = !m_statusOpen;
        ApplyFoldedCards();
    }
    void DownloadsPage::OnWaitingToggleClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_waitingOpen = !m_waitingOpen;
        ApplyFoldedCards();
    }
    void DownloadsPage::OnToggleDetailsClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto const id = TagOf(sender); !id.empty()) m_viewModel.ToggleDetails(id);
    }

    // A ready card plays when clicked or when its play button is pressed.
    void DownloadsPage::OnCardPlayRequested(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const id = CardIdOf(sender);
        if (id.empty()) return;
        m_viewModel.Select(id);
        m_viewModel.Play(id);
    }
    // The card's right-click menu. Reveal and delete act on the selection, so
    // the card is selected first.
    void DownloadsPage::OnReadyPlayClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const id = TagOf(sender);
        if (id.empty()) return;
        m_viewModel.Select(id);
        m_viewModel.Play(id);
    }
    void DownloadsPage::OnReadyRevealClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const id = TagOf(sender);
        if (id.empty()) return;
        m_viewModel.Select(id);
        m_viewModel.RevealSelected();
    }
    void DownloadsPage::OnReadyDeleteClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const id = TagOf(sender);
        if (id.empty()) return;
        m_viewModel.Select(id);
        ShowDeleteDialog();
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
    void DownloadsPage::OnPauseRowClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Pause(TagOf(sender)); }
    void DownloadsPage::OnResumeRowClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Resume(TagOf(sender)); }
    void DownloadsPage::OnRetryRowClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Retry(TagOf(sender)); }
    void DownloadsPage::OnChooseSourceRowClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.ChooseSourceFor(TagOf(sender)); }
    void DownloadsPage::OnCancelRowClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (auto const id = TagOf(sender); !id.empty()) ShowCancelDialog(id);
    }
    void DownloadsPage::OnManageFolderClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { PickDownloadFolder(); }
    void DownloadsPage::OnOpenFolderClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.OpenDownloadFolder(); }
    void DownloadsPage::OnDeleteAllClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ShowDeleteAllDialog(); }
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

    // Everything this account has on the PC goes, so it is asked first, with
    // the count, and the safe answer is the default.
    winrt::fire_and_forget DownloadsPage::ShowDeleteAllDialog()
    {
        auto lifetime = get_strong();
        if (!m_viewModel.CanDeleteAll()) co_return;
        try
        {
            auto const count = m_viewModel.FilterAllCount();
            auto dialog = ::HaloDesktop::Views::MakeDialog(
                XamlRoot(),
                ActualTheme(),
                L"Delete all downloads?",
                winrt::hstring{ L"All " + std::wstring{ count } + (count == L"1" ? L" download goes" : L" downloads go")
                    + L" from this PC, including any still downloading. The files are deleted and cannot be recovered." },
                ::HaloDesktop::Views::DialogTone::Destructive);
            dialog.PrimaryButtonText(L"Delete all");
            dialog.CloseButtonText(L"Cancel");
            dialog.DefaultButton(Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
            if (co_await dialog.ShowAsync() == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
            {
                m_viewModel.DeleteAll();
            }
        }
        catch (...)
        {
            // Only one dialog can be open at a time; nothing was deleted.
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
