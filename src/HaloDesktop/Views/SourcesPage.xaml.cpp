#include "pch.h"
#include "Views/SourcesPage.xaml.h"
#include "Controls/SmoothWheelScroll.h"
#if __has_include("SourcesPage.g.cpp")
#include "SourcesPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Views/PageDialog.h"
#include "Services/NavigationService.h"
#include "ViewModels/SourcesViewModel.h"

#include <algorithm>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>

namespace
{
    // How much of the sheet a selected card may leave before the list follows it,
    // and the lead it keeps above it once it does: the reference's 12 and 28.
    constexpr double SelectionMargin = 12.0;
    constexpr double SelectionLead = 28.0;

    // The content's own margin above and below the column, which the minimum
    // height that lets the empty state centre itself must leave room for.
    constexpr double ListVerticalMargin = 2.0 + 26.0;

    // The filter pills and sort entries, in the view model's index order.
    constexpr std::int32_t FilterAll = 0;
    constexpr std::int32_t FilterUltraHd = 1;
    constexpr std::int32_t FilterFullHd = 2;
    constexpr std::int32_t FilterHd = 3;
    constexpr std::int32_t SortRecommended = 0;
    constexpr std::int32_t SortBestPicture = 1;
    constexpr std::int32_t SortSmallestFile = 2;
    constexpr std::int32_t SortFastestStart = 3;

    constexpr wchar_t const* GenericStartFailure = L"Check the source, free storage, and download folder, then try again.";

    winrt::hstring TagOf(winrt::Windows::Foundation::IInspectable const& sender)
    {
        auto const element = sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
        return element ? winrt::unbox_value_or<winrt::hstring>(element.Tag(), L"") : winrt::hstring{};
    }

    winrt::hstring DetailsKeyOf(winrt::Windows::Foundation::IInspectable const& sender)
    {
        auto const panel = sender.try_as<winrt::HaloDesktop::SourceDetailsPanel>();
        auto const details = panel ? panel.Details() : nullptr;
        return details ? details.Key() : winrt::hstring{};
    }
}

namespace winrt::HaloDesktop::implementation
{
    SourcesPage::SourcesPage()
        : m_viewModel(winrt::make<SourcesViewModel>(App::Services()))
    {
    }

    winrt::HaloDesktop::SourcesViewModel SourcesPage::ViewModel() const { return m_viewModel; }

    void SourcesPage::OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args)
    {
        m_downloadOperation.NavigatedTo();
        m_viewModel.Load(args.Parameter());
    }

    void SourcesPage::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ::HaloDesktop::Controls::AttachSmoothWheelScroll(ListScroller());
        m_downloadOperation.Loaded();
        auto const viewModel = winrt::get_self<SourcesViewModel>(m_viewModel);
        viewModel->Activate();
        SourceList().ItemsSource(viewModel->ItemsView());

        AttachKeyHandlers();
        // The sheet owns the arrow keys while it is open, which only works if the
        // focus is inside it when it appears.
        SheetRoot().IsTabStop(true);
        static_cast<void>(SheetRoot().Focus(Microsoft::UI::Xaml::FocusState::Programmatic));

        EnterStoryboard().Begin();
        SkeletonStoryboard().Begin();
    }

    void SourcesPage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_downloadOperation.Unloaded();
        DetachKeyHandlers();
        try { SkeletonStoryboard().Stop(); }
        catch (winrt::hresult_error const&) {}
        winrt::get_self<SourcesViewModel>(m_viewModel)->Deactivate();
    }

    void SourcesPage::AttachKeyHandlers()
    {
        if (m_keyDownHandler) return;
        // The arrows are taken on the way down, before the list's scroller or a
        // radio group can claim them for scrolling or focus moves; the sheet
        // gives them its own meaning. Enter and Escape are taken on the way back
        // up, so a focused button keeps its own Enter.
        m_previewKeyDownHandler = winrt::box_value(Microsoft::UI::Xaml::Input::KeyEventHandler{
            [weak = get_weak()](
                winrt::Windows::Foundation::IInspectable const& sender,
                Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
            {
                if (auto const self = weak.get()) self->OnSheetPreviewKeyDown(sender, args);
            } });
        m_keyDownHandler = winrt::box_value(Microsoft::UI::Xaml::Input::KeyEventHandler{
            [weak = get_weak()](
                winrt::Windows::Foundation::IInspectable const& sender,
                Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
            {
                if (auto const self = weak.get()) self->OnSheetKeyDown(sender, args);
            } });
        SheetRoot().AddHandler(Microsoft::UI::Xaml::UIElement::PreviewKeyDownEvent(), m_previewKeyDownHandler, false);
        SheetRoot().AddHandler(Microsoft::UI::Xaml::UIElement::KeyDownEvent(), m_keyDownHandler, true);
    }

    void SourcesPage::DetachKeyHandlers() noexcept
    {
        try
        {
            if (m_previewKeyDownHandler)
            {
                SheetRoot().RemoveHandler(Microsoft::UI::Xaml::UIElement::PreviewKeyDownEvent(), m_previewKeyDownHandler);
            }
            if (m_keyDownHandler)
            {
                SheetRoot().RemoveHandler(Microsoft::UI::Xaml::UIElement::KeyDownEvent(), m_keyDownHandler);
            }
        }
        catch (...)
        {
        }
        m_previewKeyDownHandler = nullptr;
        m_keyDownHandler = nullptr;
    }

    void SourcesPage::OnSheetPreviewKeyDown(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        using winrt::Windows::System::VirtualKey;
        switch (args.Key())
        {
        case VirtualKey::Up:
            m_viewModel.MoveSelection(-1);
            RevealSelection();
            break;
        case VirtualKey::Down:
            m_viewModel.MoveSelection(1);
            RevealSelection();
            break;
        case VirtualKey::Right:
            m_viewModel.ExpandSelected();
            break;
        case VirtualKey::Left:
            m_viewModel.CollapseSelected();
            break;
        default:
            return;
        }
        args.Handled(true);
    }

    void SourcesPage::OnSheetKeyDown(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        using winrt::Windows::System::VirtualKey;
        switch (args.Key())
        {
        case VirtualKey::Escape:
            BeginClose();
            args.Handled(true);
            return;
        case VirtualKey::Enter:
            // Enter on a focused button is that button's: a row plays itself, a
            // chevron opens its details, Save saves. The sheet plays otherwise.
            if (args.Handled() || args.OriginalSource().try_as<Microsoft::UI::Xaml::Controls::Primitives::ButtonBase>())
            {
                return;
            }
            m_viewModel.PlaySelected();
            args.Handled(true);
            return;
        default:
            return;
        }
    }

    void SourcesPage::RevealSelection()
    {
        auto const scroller = ListScroller();
        if (!scroller) return;

        auto const index = m_viewModel.SelectedIndex();
        if (index < 0)
        {
            scroller.ScrollTo(0.0, 0.0);
            return;
        }

        auto const container = SourceList().ContainerFromIndex(index).try_as<Microsoft::UI::Xaml::FrameworkElement>();
        if (!container) return;

        auto const transform = container.TransformToVisual(scroller);
        auto const top = static_cast<double>(transform.TransformPoint(winrt::Windows::Foundation::Point{ 0.0f, 0.0f }).Y);
        auto const bottom = top + container.ActualHeight();
        auto const viewport = scroller.ViewportHeight();
        if (top >= SelectionMargin && bottom <= viewport - SelectionMargin) return;

        auto const offset = (std::max)(0.0, scroller.VerticalOffset() + top - SelectionLead);
        scroller.ScrollTo(scroller.HorizontalOffset(), offset);
    }

    void SourcesPage::OnScrollerSizeChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        // The column is at least as tall as the scroller, so an empty state can
        // sit in the middle of it as the reference's does.
        ListContent().MinHeight((std::max)(0.0, static_cast<double>(args.NewSize().Height) - ListVerticalMargin));
    }

    void SourcesPage::BeginClose()
    {
        if (m_closing) return;
        m_closing = true;
        ExitStoryboard().Begin();
    }

    // Queued rather than called straight from the animation callback: closing tears
    // down this page, and doing that inside the storyboard's own completion is a
    // needless way to unwind a live callback.
    void SourcesPage::OnExitCompleted(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&)
    {
        DispatcherQueue().TryEnqueue([weak = get_weak()]()
        {
            if (auto const self = weak.get()) self->m_viewModel.Close();
        });
    }

    void SourcesPage::OnScrimTapped(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args)
    {
        args.Handled(true);
        BeginClose();
    }

    void SourcesPage::OnCloseClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        BeginClose();
    }

    void SourcesPage::OnRetryClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.Retry();
        SyncSelectors();
    }

    void SourcesPage::OnManageAddonsClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.OpenSettings();
    }

    void SourcesPage::OnPlayOfflineClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (!m_viewModel.PlayOffline()) ShowOfflineMissingAsync();
    }

    void SourcesPage::OnAllFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(FilterAll); }
    void SourcesPage::OnUltraHdFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(FilterUltraHd); }
    void SourcesPage::OnFullHdFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(FilterFullHd); }
    void SourcesPage::OnHdFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFilter(FilterHd); }
    void SourcesPage::OnRecommendedSortClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetSort(SortRecommended); }
    void SourcesPage::OnBestPictureSortClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetSort(SortBestPicture); }
    void SourcesPage::OnSmallestFileSortClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetSort(SortSmallestFile); }
    void SourcesPage::OnFastestStartSortClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetSort(SortFastestStart); }

    // A retry resets the sort and the filter, so the two selector controls have to
    // be told; they own their own checked state, not the view model.
    void SourcesPage::SyncSelectors()
    {
        AllFilter().IsChecked(true);
        RecommendedSort().IsChecked(true);
    }

    void SourcesPage::OnPickDetailsClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.TogglePickExpanded();
    }

    void SourcesPage::OnPlayPickClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SelectPick();
        m_viewModel.OpenPlayer(winrt::get_self<SourcesViewModel>(m_viewModel)->PickKey());
    }

    void SourcesPage::OnPickSaveClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        StartDownload(winrt::get_self<SourcesViewModel>(m_viewModel)->PickKey());
    }

    void SourcesPage::OnRevealColdClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.RevealCold();
    }

    void SourcesPage::OnRowPlayClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.OpenPlayer(TagOf(sender));
    }

    void SourcesPage::OnRowChevronClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.ToggleExpanded(TagOf(sender));
    }

    void SourcesPage::OnRowFocused(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SelectKey(TagOf(sender));
    }

    void SourcesPage::OnDetailsSaveClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        StartDownload(DetailsKeyOf(sender));
    }

    void SourcesPage::OnDetailsCopyClick(winrt::Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const key = DetailsKeyOf(sender);
        auto const name = m_viewModel.FileNameFor(key);
        if (name.empty()) return;
        try
        {
            winrt::Windows::ApplicationModel::DataTransfer::DataPackage package;
            package.RequestedOperation(winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
            package.SetText(name);
            winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
            m_viewModel.MarkCopied(key);
        }
        catch (winrt::hresult_error const&)
        {
            // Another app holding the clipboard; the label simply stays "Copy file name".
            m_viewModel.MarkCopied(L"");
        }
    }

    winrt::fire_and_forget SourcesPage::StartDownload(winrt::hstring key)
    {
        auto lifetime = get_strong();
        if (key.empty())
        {
            co_return;
        }

        auto const ticket = m_downloadOperation.TryBegin();
        if (!ticket)
        {
            co_return;
        }
        auto const uiContext = winrt::apartment_context{};
        auto const viewModel = winrt::get_self<SourcesViewModel>(m_viewModel);
        // The source reads "Saving" while its start is in flight, as the reference's does.
        viewModel->SetStartingKey(key);
        try
        {
            co_await StartDownloadCore(std::move(key), *ticket);
        }
        catch (...)
        {
        }

        try
        {
            co_await uiContext;
            viewModel->SetStartingKey(L"");
            m_downloadOperation.Complete(*ticket);
        }
        catch (...)
        {
        }
    }

    winrt::Windows::Foundation::IAsyncAction SourcesPage::StartDownloadCore(
        winrt::hstring key,
        ::HaloDesktop::Services::Downloads::DownloadPageOperationState::Ticket ticket)
    {
        auto const uiContext = winrt::apartment_context{};
        auto const viewModel = winrt::get_self<SourcesViewModel>(m_viewModel);
        ::HaloDesktop::Services::DownloadStartResult result;
        try
        {
            result = co_await viewModel->StartDownloadAsync(key, false);
        }
        catch (...)
        {
        }
        co_await uiContext;
        if (!m_downloadOperation.CanApply(ticket))
        {
            co_return;
        }

        // One download per video: swapping the source has to be deliberate.
        if (result.Outcome == ::HaloDesktop::Services::DownloadStartOutcome::ReplacementRequired)
        {
            auto replace = false;
            try
            {
                replace = co_await ConfirmReplacementAsync();
            }
            catch (...)
            {
                co_return;
            }
            co_await uiContext;
            if (!m_downloadOperation.CanApply(ticket) || !replace)
            {
                co_return;
            }

            result = {};
            try
            {
                result = co_await viewModel->StartDownloadAsync(key, true);
            }
            catch (...)
            {
            }
            co_await uiContext;
            if (!m_downloadOperation.CanApply(ticket))
            {
                co_return;
            }
        }
        if (result.Outcome == ::HaloDesktop::Services::DownloadStartOutcome::Started
            || result.Outcome == ::HaloDesktop::Services::DownloadStartOutcome::AlreadyExists)
        {
            // The sheet is over the page the viewer came from, and the transfer they
            // just started is on another one, so the sheet gets out of the way.
            App::Services().Navigation->CloseSheet();
            App::Services().Navigation->GoTo(::HaloDesktop::Services::Page::Downloads);
            co_return;
        }
        viewModel->SetStartingKey(L"");
        co_await ShowDownloadFailureAsync(std::move(result.Reason));
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> SourcesPage::ConfirmReplacementAsync()
    {
        auto dialog = ::HaloDesktop::Views::MakeDialog(
            XamlRoot(),
            ActualTheme(),
            L"Replace the saved source?",
            L"Halo will keep the current file until the replacement finishes successfully.");
        dialog.PrimaryButtonText(L"Replace");
        dialog.CloseButtonText(L"Keep current");
        dialog.DefaultButton(Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
        co_return co_await dialog.ShowAsync()
            == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary;
    }

    winrt::Windows::Foundation::IAsyncAction SourcesPage::ShowDownloadFailureAsync(std::wstring reason)
    {
        // The engine's own reason names what to fix ("Sign in before downloading.",
        // "There is not enough free space for this video."); anything without one
        // gets the general advice.
        auto dialog = ::HaloDesktop::Views::MakeDialog(
            XamlRoot(),
            ActualTheme(),
            L"Download could not start",
            reason.empty() ? winrt::hstring{ GenericStartFailure } : winrt::hstring{ reason });
        dialog.CloseButtonText(L"Close");
        co_await dialog.ShowAsync();
    }

    winrt::fire_and_forget SourcesPage::ShowOfflineMissingAsync()
    {
        auto lifetime = get_strong();
        try
        {
            auto dialog = ::HaloDesktop::Views::MakeDialog(
                XamlRoot(),
                ActualTheme(),
                L"Download action failed",
                L"This download is no longer on this device.");
            dialog.CloseButtonText(L"Close");
            co_await dialog.ShowAsync();
        }
        catch (winrt::hresult_error const&)
        {
            // Another dialog is already open; the card has gone by then anyway.
        }
    }
}
