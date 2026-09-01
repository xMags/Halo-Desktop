#include "pch.h"
#include "Views/SourcesPage.xaml.h"
#if __has_include("SourcesPage.g.cpp")
#include "SourcesPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Views/PageDialog.h"
#include "Services/NavigationService.h"
#include "ViewModels/SourcesViewModel.h"

#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>

namespace
{
    // How much of the sheet has to be above or below a selected card before the
    // list scrolls to bring it back, plus the lead it leaves once it does.
    constexpr double SelectionMargin = 12.0;
    constexpr double SelectionLead = 28.0;

    winrt::hstring TagOf(winrt::Windows::Foundation::IInspectable const& sender)
    {
        auto const element = sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
        return element ? winrt::unbox_value_or<winrt::hstring>(element.Tag(), L"") : winrt::hstring{};
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
        m_downloadOperation.Loaded();
        auto const viewModel = winrt::get_self<SourcesViewModel>(m_viewModel);
        viewModel->Activate();
        FindName(L"SourceList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->ItemsView());
        FindName(L"ProviderList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->ProviderItems());
        FindName(L"QualityList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->QualityItems());
        FindName(L"PickerRuleList").as<Microsoft::UI::Xaml::Controls::ItemsControl>().ItemsSource(viewModel->PickerRules());

        AttachKeyHandler();
        // The sheet owns the arrow keys while it is open, which only works if the
        // focus is inside it when it appears.
        auto const root = FindName(L"SheetRoot").as<Microsoft::UI::Xaml::Controls::Grid>();
        root.IsTabStop(true);
        static_cast<void>(root.Focus(Microsoft::UI::Xaml::FocusState::Programmatic));

        FindName(L"EnterStoryboard").as<Microsoft::UI::Xaml::Media::Animation::Storyboard>().Begin();
        FindName(L"SkeletonStoryboard").as<Microsoft::UI::Xaml::Media::Animation::Storyboard>().Begin();
    }

    void SourcesPage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_downloadOperation.Unloaded();
        DetachKeyHandler();
        try { FindName(L"SkeletonStoryboard").as<Microsoft::UI::Xaml::Media::Animation::Storyboard>().Stop(); }
        catch (...) {}
        winrt::get_self<SourcesViewModel>(m_viewModel)->Deactivate();
    }

    void SourcesPage::AttachKeyHandler()
    {
        if (m_keyDownHandler) return;
        // Registered with handledEventsToo, because XAML's own directional focus
        // navigation marks the arrow keys handled before they reach the page.
        m_keyDownHandler = winrt::box_value(Microsoft::UI::Xaml::Input::KeyEventHandler{
            [weak = get_weak()](
                winrt::Windows::Foundation::IInspectable const& sender,
                Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
            {
                if (auto const self = weak.get()) self->OnSheetKeyDown(sender, args);
            } });
        FindName(L"SheetRoot")
            .as<Microsoft::UI::Xaml::UIElement>()
            .AddHandler(Microsoft::UI::Xaml::UIElement::KeyDownEvent(), m_keyDownHandler, true);
    }

    void SourcesPage::DetachKeyHandler() noexcept
    {
        if (!m_keyDownHandler) return;
        try
        {
            FindName(L"SheetRoot")
                .as<Microsoft::UI::Xaml::UIElement>()
                .RemoveHandler(Microsoft::UI::Xaml::UIElement::KeyDownEvent(), m_keyDownHandler);
        }
        catch (...)
        {
        }
        m_keyDownHandler = nullptr;
    }

    void SourcesPage::OnSheetKeyDown(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        using winrt::Windows::System::VirtualKey;
        auto const key = args.Key();
        // Enter and Escape belong to whatever already claimed them: a focused
        // button, or an open flyout. The arrows are taken back on purpose, because
        // the only thing that claims them is XAML's directional focus navigation,
        // which is not what the sheet wants them to mean.
        if (args.Handled() && key != VirtualKey::Up && key != VirtualKey::Down
            && key != VirtualKey::Left && key != VirtualKey::Right)
        {
            return;
        }
        switch (key)
        {
        case VirtualKey::Escape:
            BeginClose();
            break;
        case VirtualKey::Enter:
            m_viewModel.PlaySelected();
            break;
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

    void SourcesPage::RevealSelection()
    {
        auto const scroller = FindName(L"ListScroller").try_as<Microsoft::UI::Xaml::Controls::ScrollViewer>();
        if (!scroller) return;

        auto const index = m_viewModel.SelectedIndex();
        if (index < 0)
        {
            scroller.ChangeView(nullptr, 0.0, nullptr);
            return;
        }

        auto const list = FindName(L"SourceList").try_as<Microsoft::UI::Xaml::Controls::ItemsControl>();
        if (!list) return;
        auto const container = list.ContainerFromIndex(index).try_as<Microsoft::UI::Xaml::FrameworkElement>();
        if (!container) return;

        auto const transform = container.TransformToVisual(scroller);
        auto const top = transform.TransformPoint(winrt::Windows::Foundation::Point{ 0.0f, 0.0f }).Y;
        auto const bottom = top + static_cast<float>(container.ActualHeight());
        auto const viewport = static_cast<float>(scroller.ViewportHeight());
        if (top >= SelectionMargin && bottom <= viewport - SelectionMargin) return;

        auto const offset = scroller.VerticalOffset() + top - SelectionLead;
        scroller.ChangeView(nullptr, winrt::box_value(offset).as<winrt::Windows::Foundation::IReference<double>>(), nullptr);
    }

    void SourcesPage::BeginClose()
    {
        if (m_closing) return;
        m_closing = true;
        FindName(L"ExitStoryboard").as<Microsoft::UI::Xaml::Media::Animation::Storyboard>().Begin();
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

    void SourcesPage::OnEditPlaybackClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.OpenSettings();
    }

    void SourcesPage::OnToggleInfoClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.ToggleInfo();
    }

    void SourcesPage::OnAllFilterClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetFilter(0);
    }
    void SourcesPage::OnPlaysNowFilterClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetFilter(1);
    }
    void SourcesPage::OnUltraHdFilterClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetFilter(2);
    }
    void SourcesPage::OnFullHdFilterClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetFilter(3);
    }
    void SourcesPage::OnHdFilterClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetFilter(4);
    }

    void SourcesPage::OnRecommendedSortClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetSort(0);
    }
    void SourcesPage::OnBestPictureSortClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetSort(1);
    }
    void SourcesPage::OnSmallestFileSortClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetSort(2);
    }
    void SourcesPage::OnFastestStartSortClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SetSort(3);
    }

    // A retry resets the sort and the filter, so the two selector controls have to
    // be told; they own their own checked state, not the view model.
    void SourcesPage::SyncSelectors()
    {
        if (auto const pill = FindName(L"AllFilter").try_as<Microsoft::UI::Xaml::Controls::RadioButton>())
        {
            pill.IsChecked(true);
        }
        if (auto const sort = FindName(L"RecommendedSort")
                .try_as<Microsoft::UI::Xaml::Controls::RadioMenuFlyoutItem>())
        {
            sort.IsChecked(true);
        }
    }

    void SourcesPage::OnCardClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.ToggleExpanded(TagOf(sender));
    }

    void SourcesPage::OnPickDetailsClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.TogglePickExpanded();
    }

    void SourcesPage::OnRevealColdClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.RevealCold();
    }

    void SourcesPage::OnPlayPickClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.SelectPick();
        m_viewModel.PlaySelected();
    }

    void SourcesPage::OnPlaySourceClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.OpenPlayer(TagOf(sender));
    }

    void SourcesPage::OnDownloadPickClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        StartDownload(winrt::get_self<SourcesViewModel>(m_viewModel)->PickKey());
    }

    void SourcesPage::OnDownloadSourceClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        StartDownload(TagOf(sender));
    }

    void SourcesPage::OnCopyFileNameClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        auto const name = m_viewModel.FileNameFor(TagOf(sender));
        if (name.empty()) return;
        winrt::Windows::ApplicationModel::DataTransfer::DataPackage package;
        package.RequestedOperation(winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
        package.SetText(name);
        winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
        m_viewModel.MarkCopied(TagOf(sender));
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
        auto outcome = ::HaloDesktop::Services::DownloadStartOutcome::Failed;
        try
        {
            outcome = co_await viewModel->StartDownloadAsync(key, false);
        }
        catch (...)
        {
        }
        co_await uiContext;
        if (!m_downloadOperation.CanApply(ticket))
        {
            co_return;
        }

        if (outcome == ::HaloDesktop::Services::DownloadStartOutcome::ReplacementRequired)
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

            outcome = ::HaloDesktop::Services::DownloadStartOutcome::Failed;
            try
            {
                outcome = co_await viewModel->StartDownloadAsync(key, true);
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
        if (outcome == ::HaloDesktop::Services::DownloadStartOutcome::Started
            || outcome == ::HaloDesktop::Services::DownloadStartOutcome::AlreadyExists)
        {
            // The sheet is over the page the viewer came from, and the transfer they
            // just started is on another one, so the sheet gets out of the way.
            App::Services().Navigation->CloseSheet();
            App::Services().Navigation->GoTo(::HaloDesktop::Services::Page::Downloads);
            co_return;
        }
        co_await ShowDownloadFailureAsync();
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

    winrt::Windows::Foundation::IAsyncAction SourcesPage::ShowDownloadFailureAsync()
    {
        auto dialog = ::HaloDesktop::Views::MakeDialog(
            XamlRoot(),
            ActualTheme(),
            L"Download could not start",
            L"Check the source, free storage, and download folder, then try again.");
        dialog.CloseButtonText(L"Close");
        co_await dialog.ShowAsync();
    }
}
