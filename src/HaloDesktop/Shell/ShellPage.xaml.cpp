#include "pch.h"
#include "Shell/ShellPage.xaml.h"
#if __has_include("ShellPage.g.cpp")
#include "ShellPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Services/AccountLabelPolicy.h"
#include "Services/AccountPictureService.h"
#include "Services/Device/DeviceDataMove.h"
#include "Services/Device/DeviceMovePolicy.h"
#include "Services/DevicePreferencesStore.h"
#include "Services/NavigationService.h"
#include "Shell/LayoutMetricsService.h"
#include "Shell/PointerFocusPolicy.h"
#include "Views/AccountPictureDecoder.h"
#include "Views/PageDialog.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <string_view>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

namespace
{
    // The avatar's drawn size; the picture is decoded for it rather than at the
    // size the account uploaded.
    constexpr std::uint32_t AvatarSize = 28;
    constexpr wchar_t AccountToolTip[] = L"Server & account";

    winrt::Microsoft::UI::Xaml::FrameworkElement FindNamedDescendant(
        winrt::Microsoft::UI::Xaml::DependencyObject const& root,
        std::wstring_view name)
    {
        auto const count = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChildrenCount(root);
        for (std::int32_t index = 0; index < count; ++index)
        {
            auto const child = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChild(root, index);
            if (auto const element = child.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
                element && element.Name() == name)
            {
                return element;
            }
            if (auto const found = FindNamedDescendant(child, name))
            {
                return found;
            }
        }
        return nullptr;
    }
}

namespace winrt::HaloDesktop::implementation
{
    ShellPage::ShellPage()
        : m_titleMenu(App::Services())
    {
    }

    void ShellPage::InitializeComponent()
    {
        ShellPageT::InitializeComponent();
        m_stockAvatar = AccountAvatar().Source();
        // Closed until the viewer first opens it, then as they last left it.
        auto const open = App::Services().DevicePreferences->NavPaneOpen();
        m_storedPaneOpen = open;
        NavigationControl().IsPaneOpen(open);
        ApplyPaneState(open);
    }

    void ShellPage::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        if (m_attached)
        {
            return;
        }

        m_attached = true;
        AttachPointerHandlers();

        // XAML settles pointer focus after the press has finished bubbling, and when
        // the press landed on something that cannot hold focus it settles on the
        // first tab stop in the page. On a page whose first tab stop is a text box
        // (Home's header search, Search, History, Settings) a click on empty
        // background put a caret there and dragged the scroll up to show it.
        // Nothing can undo that afterwards, because anything this shell does during
        // the press is what gets overridden, so the move is refused instead.
        // The rule must stay free of page and control names and of any type beyond
        // "is a text input" (PointerFocusPolicy): it once named only Home's box, and
        // every page rebuilt with another box brought the stuck caret back.
        m_gettingFocusToken = Microsoft::UI::Xaml::Input::FocusManager::GettingFocus(
            [weak = get_weak()](
                winrt::Windows::Foundation::IInspectable const&,
                Microsoft::UI::Xaml::Input::GettingFocusEventArgs const& eventArgs)
            {
                auto const self = weak.get();
                if (!self || !self->m_nonTextPointerId)
                {
                    return;
                }
                if (eventArgs.FocusState() != Microsoft::UI::Xaml::FocusState::Pointer)
                {
                    return;
                }
                auto const target = eventArgs.NewFocusedElement();
                if (!target)
                {
                    return;
                }
                auto refuse = false;
                try
                {
                    auto const targetInput = TextInputRoot(target);
                    auto const oldFocused = eventArgs.OldFocusedElement();
                    refuse = ::HaloDesktop::Shell::ShouldRefusePointerFocus({
                        .GuardActive = self->m_nonTextPointerId.has_value(),
                        .ByPointer = eventArgs.FocusState() == Microsoft::UI::Xaml::FocusState::Pointer,
                        .TargetIsTextInput = static_cast<bool>(targetInput),
                        .TargetInShellContent = targetInput && self->IsInShellContent(target),
                        .TargetAlreadyFocused = targetInput && oldFocused && TextInputRoot(oldFocused) == targetInput,
                    });
                }
                catch (winrt::hresult_error const&)
                {
                    // An element leaving the tree mid-walk: let XAML have its way.
                }
                if (!refuse)
                {
                    return;
                }

                auto redirected = false;
                try
                {
                    redirected = eventArgs.TrySetNewFocusedElement(self->FocusSink());
                }
                catch (...)
                {
                }
                if (!redirected)
                {
                    static_cast<void>(eventArgs.TryCancel());
                }
            });

        auto const navigation = App::Services().Navigation;
        navigation->AttachShellFrame(ContentFrameControl());
        m_frameNavigatedRevoker = ContentFrameControl().Navigated(
            winrt::auto_revoke,
            [this](
                winrt::Windows::Foundation::IInspectable const& frame,
                Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& eventArgs)
            {
                OnFrameNavigated(frame, eventArgs);
            });

        navigation->GoTo(::HaloDesktop::Services::Page::Home);
        m_downloadChangedToken = App::Services().Downloads->AddChangedHandler(
            [weak = get_weak()]()
            {
                if (auto const self = weak.get())
                {
                    self->UpdateDownloadBadge();
                }
            });
        // The sidebar reads the same collection the Home shelf does, so it needs
        // the same notice when a next-episode lookup lands after the first paint.
        m_continueChangedToken = App::Services().Catalog->AddContinueChangedHandler(
            [weak = get_weak()]()
            {
                if (auto const self = weak.get())
                {
                    self->RefreshJumpBackIn();
                }
            });
        // A right-click menu anywhere can mark one of these episodes watched,
        // which moves its row on the way finishing it would.
        m_userStateChangedToken = App::Services().Catalog->AddUserStateChangedHandler(
            [weak = get_weak()]()
            {
                if (auto const self = weak.get())
                {
                    self->RefreshJumpBackIn();
                }
            });
        // What was read above belongs to whoever was signed in at launch, often
        // nobody, with the sign-in page over the shell.
        m_identityChangedToken = App::Services().Session->AddIdentityChangedHandler(
            [weak = get_weak()]()
            {
                if (auto const self = weak.get())
                {
                    self->OnIdentityChanged();
                }
            });
        // The tier and the picture arrive with /auth/me, which for the account
        // restored at launch is no change of account at all.
        m_profileChangedToken = App::Services().Session->AddProfileChangedHandler(
            [weak = get_weak()]()
            {
                if (auto const self = weak.get())
                {
                    self->RefreshAccountIdentity();
                }
            });
        // Moving this PC's data into an account can skip an addon the account
        // could not load; the shell says so, whichever page is up.
        m_skippedAddonsToken = App::Services().DeviceMove->AddSkippedAddonsHandler(
            [weak = get_weak()](std::vector<std::wstring> const& names)
            {
                if (auto const self = weak.get())
                {
                    self->ShowSkippedAddons(names);
                }
            });
        UpdatePaneToggleOffset();
        HookPaneToggle();
        RestoreStoredPaneState();
        UpdateDownloadBadge();
        RefreshAccountIdentity();
        RefreshJumpBackIn();
        RefreshJumpBackInAsync();
        // Also picks up a notice still pending from before the shell last unloaded.
        ShowSkippedAddons(App::Services().DeviceMove->TakeUnreportedSkippedAddons());
    }

    void ShellPage::OnIdentityChanged()
    {
        RefreshAccountIdentity();
        // The account's services were reset before this runs, so this empties the
        // rows of the account that left; the load refills them for the new one.
        RefreshJumpBackIn();
        if (App::Services().Session->Mode() != ::HaloDesktop::Services::SessionMode::SignedOut)
        {
            RefreshJumpBackInAsync();
        }
    }

    // The names stay pending until a dialog naming them was actually opened. Only
    // one dialog can be open at a time, so while another is up the notice waits
    // and tries again; one runner at a time, on the UI thread, ending when the
    // shell unloads (OnLoaded starts it again for whatever is still pending).
    winrt::fire_and_forget ShellPage::ShowSkippedAddons(std::vector<std::wstring> names)
    {
        auto lifetime = get_strong();
        m_pendingSkippedAddons.insert(m_pendingSkippedAddons.end(), names.begin(), names.end());
        if (m_skippedNoticeRunning || m_pendingSkippedAddons.empty())
        {
            co_return;
        }
        m_skippedNoticeRunning = true;
        try
        {
            auto const uiContext = winrt::apartment_context{};
            while (m_attached && !m_pendingSkippedAddons.empty())
            {
                auto const batch = m_pendingSkippedAddons;
                winrt::Windows::Foundation::IAsyncOperation<Microsoft::UI::Xaml::Controls::ContentDialogResult> showing{ nullptr };
                try
                {
                    auto dialog = ::HaloDesktop::Views::MakeDialog(
                        XamlRoot(),
                        ActualTheme(),
                        winrt::hstring{ ::HaloDesktop::Services::Device::FailedAddonsTitle(batch.size()) },
                        winrt::hstring{ ::HaloDesktop::Services::Device::FailedAddonsBody(batch) });
                    dialog.CloseButtonText(L"OK");
                    showing = dialog.ShowAsync();
                }
                catch (winrt::hresult_error const&)
                {
                    // Another dialog is open, or the shell has no window yet.
                }
                if (!showing)
                {
                    co_await winrt::resume_after(std::chrono::seconds(2));
                    co_await uiContext;
                    continue;
                }
                // Shown: these names are told. Any that arrived meanwhile were
                // appended after them and get a dialog of their own.
                m_pendingSkippedAddons.erase(
                    m_pendingSkippedAddons.begin(),
                    m_pendingSkippedAddons.begin() + static_cast<std::ptrdiff_t>(batch.size()));
                try
                {
                    static_cast<void>(co_await showing);
                }
                catch (winrt::hresult_error const&)
                {
                    // Closed with the window.
                }
                co_await uiContext;
            }
        }
        catch (...)
        {
        }
        m_skippedNoticeRunning = false;
    }

    void ShellPage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        if (m_downloadChangedToken != 0)
        {
            App::Services().Downloads->RemoveChangedHandler(m_downloadChangedToken);
            m_downloadChangedToken = 0;
        }
        if (m_continueChangedToken != 0)
        {
            App::Services().Catalog->RemoveContinueChangedHandler(m_continueChangedToken);
            m_continueChangedToken = 0;
        }
        if (m_userStateChangedToken != 0)
        {
            App::Services().Catalog->RemoveUserStateChangedHandler(m_userStateChangedToken);
            m_userStateChangedToken = 0;
        }
        if (m_identityChangedToken != 0)
        {
            App::Services().Session->RemoveIdentityChangedHandler(m_identityChangedToken);
            m_identityChangedToken = 0;
        }
        if (m_profileChangedToken != 0)
        {
            App::Services().Session->RemoveProfileChangedHandler(m_profileChangedToken);
            m_profileChangedToken = 0;
        }
        if (m_skippedAddonsToken != 0)
        {
            App::Services().DeviceMove->RemoveSkippedAddonsHandler(m_skippedAddonsToken);
            m_skippedAddonsToken = 0;
        }
        // A picture still loading has nowhere to go.
        ++m_pictureRequest;
        if (m_gettingFocusToken)
        {
            Microsoft::UI::Xaml::Input::FocusManager::GettingFocus(m_gettingFocusToken);
            m_gettingFocusToken = {};
        }
        DetachPointerHandlers();
        m_frameNavigatedRevoker.revoke();
        m_paneToggleClick.revoke();
        m_attached = false;
    }

    // The outermost text input the element is part of, so a box's inner parts and
    // a box nested in another (the TextBox inside an AutoSuggestBox) name one input.
    Microsoft::UI::Xaml::DependencyObject ShellPage::TextInputRoot(Microsoft::UI::Xaml::DependencyObject const& element)
    {
        Microsoft::UI::Xaml::DependencyObject root{ nullptr };
        auto current = element;
        while (current)
        {
            if (current.try_as<Microsoft::UI::Xaml::Controls::TextBox>() ||
                current.try_as<Microsoft::UI::Xaml::Controls::AutoSuggestBox>() ||
                current.try_as<Microsoft::UI::Xaml::Controls::RichEditBox>() ||
                current.try_as<Microsoft::UI::Xaml::Controls::PasswordBox>())
            {
                root = current;
            }
            current = Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(current);
        }
        return root;
    }

    bool ShellPage::IsWithinTextInput(Microsoft::UI::Xaml::DependencyObject const& element)
    {
        return static_cast<bool>(TextInputRoot(element));
    }

    // The nearest control that would take focus for this press decides: one that
    // declares interaction does not move focus keeps the caret where it is.
    bool ShellPage::KeepsFocusOnInteraction(Microsoft::UI::Xaml::DependencyObject const& element)
    {
        auto current = element;
        while (current)
        {
            if (auto const framework = current.try_as<Microsoft::UI::Xaml::FrameworkElement>();
                framework && !framework.AllowFocusOnInteraction())
            {
                return true;
            }
            if (auto const control = current.try_as<Microsoft::UI::Xaml::Controls::Control>();
                control && control.IsTabStop())
            {
                return false;
            }
            current = Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(current);
        }
        return false;
    }

    // Dialogs, flyouts and popups hang off the window's popup root, and the sheet
    // and overlay frames are siblings of the shell: none of them is under the
    // content host, which is also the only place the press handlers listen.
    bool ShellPage::IsInShellContent(Microsoft::UI::Xaml::DependencyObject const& element)
    {
        auto const host = ContentHost().as<Microsoft::UI::Xaml::DependencyObject>();
        auto current = element;
        while (current)
        {
            if (current == host)
            {
                return true;
            }
            current = Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(current);
        }
        return false;
    }

    void ShellPage::AttachPointerHandlers()
    {
        auto const weak = get_weak();
        m_pointerPressedHandler = winrt::box_value(Microsoft::UI::Xaml::Input::PointerEventHandler{
            [weak](
                winrt::Windows::Foundation::IInspectable const& sender,
                Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
            {
                if (auto const self = weak.get())
                {
                    self->OnContentPointerPressed(sender, args);
                }
            } });
        m_pointerEndedHandler = winrt::box_value(Microsoft::UI::Xaml::Input::PointerEventHandler{
            [weak](
                winrt::Windows::Foundation::IInspectable const& sender,
                Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
            {
                if (auto const self = weak.get())
                {
                    self->OnContentPointerEnded(sender, args);
                }
            } });

        // The mouse back button is a shell-wide gesture, not a content one, so it
        // listens on the NavigationView rather than the frame host: a press over the
        // rail should go back just as one over the page does. Handled presses count,
        // because the button a press lands on has no interest in the X buttons.
        m_backPointerHandler = winrt::box_value(Microsoft::UI::Xaml::Input::PointerEventHandler{
            [weak](
                winrt::Windows::Foundation::IInspectable const& sender,
                Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
            {
                if (auto const self = weak.get())
                {
                    self->OnShellPointerPressed(sender, args);
                }
            } });

        auto const host = ContentHost();
        host.AddHandler(
            Microsoft::UI::Xaml::UIElement::PointerPressedEvent(),
            m_pointerPressedHandler,
            true);
        NavigationControl().AddHandler(
            Microsoft::UI::Xaml::UIElement::PointerPressedEvent(),
            m_backPointerHandler,
            true);
        for (auto const& routedEvent : {
                 Microsoft::UI::Xaml::UIElement::PointerReleasedEvent(),
                 Microsoft::UI::Xaml::UIElement::PointerCanceledEvent(),
                 Microsoft::UI::Xaml::UIElement::PointerCaptureLostEvent() })
        {
            host.AddHandler(routedEvent, m_pointerEndedHandler, true);
        }
    }

    void ShellPage::DetachPointerHandlers() noexcept
    {
        m_nonTextPointerId.reset();
        try
        {
            auto const host = ContentHost();
            if (m_pointerPressedHandler)
            {
                host.RemoveHandler(
                    Microsoft::UI::Xaml::UIElement::PointerPressedEvent(),
                    m_pointerPressedHandler);
            }
            if (m_pointerEndedHandler)
            {
                for (auto const& routedEvent : {
                         Microsoft::UI::Xaml::UIElement::PointerReleasedEvent(),
                         Microsoft::UI::Xaml::UIElement::PointerCanceledEvent(),
                         Microsoft::UI::Xaml::UIElement::PointerCaptureLostEvent() })
                {
                    host.RemoveHandler(routedEvent, m_pointerEndedHandler);
                }
            }
        }
        catch (...)
        {
        }
        try
        {
            if (m_backPointerHandler)
            {
                NavigationControl().RemoveHandler(
                    Microsoft::UI::Xaml::UIElement::PointerPressedEvent(),
                    m_backPointerHandler);
            }
        }
        catch (...)
        {
        }
        m_pointerPressedHandler = nullptr;
        m_pointerEndedHandler = nullptr;
        m_backPointerHandler = nullptr;
    }

    void ShellPage::OnContentPointerPressed(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        auto const source = args.OriginalSource().try_as<Microsoft::UI::Xaml::DependencyObject>();
        auto const focused =
            Microsoft::UI::Xaml::Input::FocusManager::GetFocusedElement(XamlRoot())
                .try_as<Microsoft::UI::Xaml::DependencyObject>();
        auto const plan = ::HaloDesktop::Shell::PlanContentPress({
            .LandedInTextInput = source && IsWithinTextInput(source),
            .LandedOnFocusKeepingControl = source && KeepsFocusOnInteraction(source),
            .CaretInTextInput = focused && IsWithinTextInput(focused),
        });
        if (!plan.GuardTextFocus)
        {
            m_nonTextPointerId.reset();
            return;
        }

        // The guard belongs to this complete pointer interaction, not to a guessed
        // dispatcher turn. A handled release, cancellation, or capture loss clears it.
        m_nonTextPointerId = args.Pointer().PointerId();

        // Refusing the move keeps a caret from appearing, but an existing one has to
        // be sent somewhere. FocusSink is a sibling of the frame, so parking focus
        // there cannot scroll a page.
        if (plan.ParkCaret)
        {
            FocusSink().Focus(Microsoft::UI::Xaml::FocusState::Pointer);
        }
    }

    void ShellPage::OnContentPointerEnded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (m_nonTextPointerId && *m_nonTextPointerId == args.Pointer().PointerId())
        {
            m_nonTextPointerId.reset();
        }
    }

    void ShellPage::OnItemInvoked(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::NavigationView const& sender,
        Microsoft::UI::Xaml::Controls::NavigationViewItemInvokedEventArgs const& args)
    {
        auto const item = args.InvokedItemContainer().try_as<Microsoft::UI::Xaml::Controls::NavigationViewItem>();
        if (!item)
        {
            return;
        }

        NavigateFromTag(winrt::unbox_value_or<winrt::hstring>(item.Tag(), L""));
    }

    void ShellPage::OnJumpContextRequested(
        winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args)
    {
        auto const row = sender.try_as<Microsoft::UI::Xaml::Controls::NavigationViewItem>();
        if (!row)
        {
            return;
        }
        auto const tag = winrt::unbox_value_or<winrt::hstring>(row.Tag(), L"");
        std::array<wchar_t const*, 3> const tags{ L"Jump0", L"Jump1", L"Jump2" };
        for (std::size_t index = 0; index < tags.size(); ++index)
        {
            if (tag == tags[index] && m_jumpItems[index])
            {
                m_titleMenu.ShowForContinue(row, args, m_jumpItems[index]);
                return;
            }
        }
    }

    void ShellPage::OnBackRequested(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::NavigationView const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::NavigationViewBackRequestedEventArgs const& args)
    {
        App::Services().Navigation->GoBack();
    }

    void ShellPage::OnSearchAcceleratorInvoked(
        [[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        // The pane no longer hosts a search box, so the shortcut opens the search page,
        // which focuses its own input on arrival.
        App::Services().Navigation->GoTo(::HaloDesktop::Services::Page::Search);
        args.Handled(true);
    }

    // Pages reached from another page carry their own back button in the section
    // header. These two are the shortcuts that work anywhere in the shell, including
    // on the rail destinations, which have no header button by design.
    void ShellPage::OnBackAcceleratorInvoked(
        [[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        args.Handled(App::Services().Navigation->GoBack());
    }

    void ShellPage::OnShellPointerPressed(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
    {
        if (args.Pointer().PointerDeviceType() != Microsoft::UI::Input::PointerDeviceType::Mouse)
        {
            return;
        }
        auto const point = args.GetCurrentPoint(nullptr);
        if (!point || !point.Properties().IsXButton1Pressed())
        {
            return;
        }
        if (App::Services().Navigation->GoBack())
        {
            args.Handled(true);
        }
    }

    void ShellPage::OnAccountClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        App::Services().Navigation->GoTo(::HaloDesktop::Services::Page::Settings);
    }

    // The frame host is the area pages actually lay out in, so it already has the
    // nav rail subtracted, whether the rail is collapsed, compact or expanded.
    void ShellPage::OnContentHostSizeChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        App::Services().LayoutMetrics->SetContentWidth(args.NewSize().Width);
    }

    void ShellPage::OnPaneOpening(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::NavigationView const& sender,
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args)
    {
        RefreshAccountIdentity();
        RefreshJumpBackIn();
        ApplyPaneState(true);
    }

    // Not remembered here: the NavigationView also opens and closes the pane on
    // its own (as it sets itself up), and storing those moves overwrote the
    // viewer's choice. Only the toggle button stores it (HookPaneToggle).
    void ShellPage::OnPaneClosing(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::NavigationView const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::NavigationViewPaneClosingEventArgs const& args)
    {
        ApplyPaneState(false);
    }

    // The viewer's own toggle is the one source of the stored pane state. The
    // NavigationView flips the pane in its own handler for the same click, so
    // the new state is read a turn later, once that has run.
    void ShellPage::HookPaneToggle()
    {
        if (m_paneToggleClick || !m_paneToggle)
        {
            return;
        }
        auto const button = m_paneToggle.try_as<Microsoft::UI::Xaml::Controls::Primitives::ButtonBase>();
        if (!button)
        {
            return;
        }
        m_paneToggleClick = button.Click(winrt::auto_revoke, [weak = get_weak()](auto const&, auto const&)
        {
            auto const self = weak.get();
            if (!self)
            {
                return;
            }
            try
            {
                static_cast<void>(self->DispatcherQueue().TryEnqueue(
                    Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
                    [weak = self->get_weak()]()
                    {
                        if (auto const shell = weak.get())
                        {
                            try
                            {
                                shell->RememberPaneState(shell->NavigationControl().IsPaneOpen());
                            }
                            catch (winrt::hresult_error const&)
                            {
                            }
                        }
                    }));
            }
            catch (winrt::hresult_error const&)
            {
                // The shell is going away: nothing to remember it for.
            }
        });
    }

    // The NavigationView settles its pane as it first lays out, after the value
    // read at start was applied, so the stored choice is put back once that has
    // happened.
    void ShellPage::RestoreStoredPaneState()
    {
        try
        {
            static_cast<void>(DispatcherQueue().TryEnqueue(
                Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
                [weak = get_weak()]()
                {
                    auto const self = weak.get();
                    if (!self || !self->m_attached)
                    {
                        return;
                    }
                    try
                    {
                        auto const navigation = self->NavigationControl();
                        if (navigation.IsPaneOpen() != self->m_storedPaneOpen)
                        {
                            navigation.IsPaneOpen(self->m_storedPaneOpen);
                        }
                    }
                    catch (winrt::hresult_error const&)
                    {
                    }
                }));
        }
        catch (winrt::hresult_error const&)
        {
        }
    }

    void ShellPage::ApplyPaneState(bool open)
    {
        SetJumpBackVisibility(open);
        AccountLines().Visibility(open
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed);
        UpdateAccountToolTip();
        UpdatePaneToggleOffset();
        m_paneShownOpen = open;
        UpdateDownloadBadge();
    }

    // The toggle belongs to the NavigationView's template, so it exists only once
    // the template has been applied; before that this does nothing and the Loaded
    // handler applies it.
    void ShellPage::UpdatePaneToggleOffset()
    {
        if (!m_paneToggle && m_attached)
        {
            m_paneToggle = FindNamedDescendant(NavigationControl(), L"TogglePaneButton");
        }
        auto const toggle = m_paneToggle;
        if (!toggle)
        {
            return;
        }
        // No margin in either pane state: the toggle's own column already centres
        // it over the rail's icons, and any offset shows as a visible misalignment.
        toggle.Margin(Microsoft::UI::Xaml::Thickness{ 0.0, 0.0, 0.0, 0.0 });
    }

    void ShellPage::RememberPaneState(bool open)
    {
        if (open == m_storedPaneOpen)
        {
            return;
        }
        m_storedPaneOpen = open;
        if (!m_paneWriteRunning)
        {
            WritePaneStateAsync();
        }
    }

    // One writer at a time, writing whatever was asked for last, so toggles faster
    // than the disk only ever leave the final state behind, never an older one.
    winrt::fire_and_forget ShellPage::WritePaneStateAsync()
    {
        auto lifetime = get_strong();
        auto const uiContext = winrt::apartment_context{};
        auto const store = App::Services().DevicePreferences;
        m_paneWriteRunning = true;
        while (true)
        {
            auto const value = m_storedPaneOpen;
            co_await winrt::resume_background();
            try
            {
                store->NavPaneOpen(value);
            }
            catch (...)
            {
                // Losing the preference is not worth failing a click over.
            }
            co_await uiContext;
            if (m_storedPaneOpen == value)
            {
                break;
            }
        }
        m_paneWriteRunning = false;
    }

    void ShellPage::SetJumpBackVisibility(bool visible)
    {
        m_paneOpen = visible;
        std::array items{ JumpItem1(), JumpItem2(), JumpItem3() };
        auto anyVisible = false;
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            auto const show = visible && static_cast<bool>(m_jumpItems[index]);
            items[index].Visibility(show
                ? Microsoft::UI::Xaml::Visibility::Visible
                : Microsoft::UI::Xaml::Visibility::Collapsed);
            anyVisible = anyVisible || show;
        }
        JumpHeader().Visibility(anyVisible
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed);
    }

    void ShellPage::OnFrameNavigated(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args)
    {
        UpdateNavigationState(App::Services().Navigation->CurrentPage());
        RefreshJumpBackIn();
    }

    Microsoft::UI::Xaml::Controls::Frame ShellPage::ContentFrameControl() const
    {
        return FindName(L"ContentFrame").as<Microsoft::UI::Xaml::Controls::Frame>();
    }

    Microsoft::UI::Xaml::Controls::InfoBadge ShellPage::DownloadsBadgeControl() const
    {
        return FindName(L"DownloadsBadge").as<Microsoft::UI::Xaml::Controls::InfoBadge>();
    }

    Microsoft::UI::Xaml::Controls::NavigationView ShellPage::NavigationControl() const
    {
        return FindName(L"Navigation").as<Microsoft::UI::Xaml::Controls::NavigationView>();
    }

    Microsoft::UI::Xaml::Controls::NavigationViewItem ShellPage::NavigationItem(winrt::hstring const& name) const
    {
        return FindName(name).as<Microsoft::UI::Xaml::Controls::NavigationViewItem>();
    }

    void ShellPage::NavigateFromTag(winrt::hstring const& tag)
    {
        using ::HaloDesktop::Services::Page;

        if (tag == L"Home")
        {
            App::Services().Navigation->GoTo(Page::Home);
            return;
        }
        if (tag == L"Search")
        {
            App::Services().Navigation->GoTo(Page::Search);
            return;
        }
        if (tag == L"Library")
        {
            App::Services().Navigation->GoTo(Page::Library);
            return;
        }
        if (tag == L"History")
        {
            App::Services().Navigation->GoTo(Page::History);
            return;
        }
        if (tag == L"Downloads")
        {
            App::Services().Navigation->GoTo(Page::Downloads);
            return;
        }
        if (tag == L"Settings")
        {
            App::Services().Navigation->GoTo(Page::Settings);
            return;
        }
        if (tag == L"Jump0" || tag == L"Jump1" || tag == L"Jump2")
        {
            auto const index = static_cast<std::size_t>(tag[4] - L'0');
            if (m_jumpItems[index])
            {
                ::HaloDesktop::Services::OpenContinueItem(
                    *App::Services().Navigation,
                    m_jumpItems[index]);
            }
            return;
        }
        if (tag == L"Detail")
        {
            App::Services().Navigation->GoTo(Page::Detail);
        }
    }

    void ShellPage::UpdateNavigationState(::HaloDesktop::Services::Page page)
    {
        using ::HaloDesktop::Services::Page;

        // A title page keeps the section it was opened from lit, as the
        // reference does. The sources sheet is a sheet over whatever page is
        // showing, so that page's section stays lit too (the reference lights the
        // stack's root, which a sheet never changes).
        auto section = page == Page::Sources ? App::Services().Navigation->CurrentPage() : page;
        if (section == Page::Detail || section == Page::Sources)
        {
            section = App::Services().Navigation->SectionBehindCurrentPage();
        }
        Microsoft::UI::Xaml::Controls::NavigationViewItem selectedItem{ nullptr };
        switch (section)
        {
        case Page::Home:
        case Page::Detail:
            selectedItem = NavigationItem(L"HomeItem");
            break;
        case Page::Search:
            selectedItem = NavigationItem(L"SearchItem");
            break;
        case Page::Catalog:
            selectedItem = NavigationControl().SelectedItem().try_as<
                Microsoft::UI::Xaml::Controls::NavigationViewItem>();
            if (!selectedItem)
            {
                selectedItem = NavigationItem(L"HomeItem");
            }
            break;
        case Page::Library:
            selectedItem = NavigationItem(L"LibraryItem");
            break;
        case Page::History:
            selectedItem = NavigationItem(L"HistoryItem");
            break;
        case Page::Downloads:
            selectedItem = NavigationItem(L"DownloadsItem");
            break;
        case Page::Settings:
            selectedItem = NavigationItem(L"SettingsItem");
            break;
        }

        NavigationControl().SelectedItem(selectedItem);
        NavigationControl().IsBackEnabled(App::Services().Navigation->CanGoBack());
    }

    void ShellPage::UpdateDownloadBadge()
    {
        auto const count = App::Services().Downloads->ActiveCount();
        auto const badge = DownloadsBadgeControl();
        badge.Value(count);
        // The count beside the label with the pane open; a dot on the icon with
        // it closed, where a number would crowd the glyph.
        badge.Visibility(count != 0 && m_paneShownOpen
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed);
        if (auto const dot = DownloadsDot())
        {
            dot.Visibility(count != 0 && !m_paneShownOpen
                ? Microsoft::UI::Xaml::Visibility::Visible
                : Microsoft::UI::Xaml::Visibility::Collapsed);
        }
    }

    void ShellPage::RefreshJumpBackIn()
    {
        std::array titles{
            FindName(L"JumpTitle1").as<Microsoft::UI::Xaml::Controls::TextBlock>(),
            FindName(L"JumpTitle2").as<Microsoft::UI::Xaml::Controls::TextBlock>(),
            FindName(L"JumpTitle3").as<Microsoft::UI::Xaml::Controls::TextBlock>(),
        };
        std::array metadata{
            FindName(L"JumpMeta1").as<Microsoft::UI::Xaml::Controls::TextBlock>(),
            FindName(L"JumpMeta2").as<Microsoft::UI::Xaml::Controls::TextBlock>(),
            FindName(L"JumpMeta3").as<Microsoft::UI::Xaml::Controls::TextBlock>(),
        };
        std::array posters{
            FindName(L"JumpPoster1").as<winrt::HaloDesktop::ArtworkImage>(),
            FindName(L"JumpPoster2").as<winrt::HaloDesktop::ArtworkImage>(),
            FindName(L"JumpPoster3").as<winrt::HaloDesktop::ArtworkImage>(),
        };
        auto const continued = App::Services().Catalog->ContinueWatching();
        for (std::size_t index = 0; index < m_jumpItems.size(); ++index)
        {
            m_jumpItems[index] = index < static_cast<std::size_t>(continued.Size())
                ? continued.GetAt(static_cast<std::uint32_t>(index))
                : nullptr;
            titles[index].Text(m_jumpItems[index] ? m_jumpItems[index].Name() : L"");
            posters[index].SourceUrl(m_jumpItems[index] ? m_jumpItems[index].Poster() : L"");
            if (!m_jumpItems[index])
            {
                metadata[index].Text(L"");
                continue;
            }
            auto line = m_jumpItems[index].Tag();
            if (!line.empty() && !m_jumpItems[index].TimeLeft().empty())
            {
                line = line + L" · ";
            }
            metadata[index].Text(line + m_jumpItems[index].TimeLeft());
        }
        SetJumpBackVisibility(m_paneOpen);
    }

    winrt::fire_and_forget ShellPage::RefreshJumpBackInAsync()
    {
        auto lifetime = get_strong();
        try
        {
            auto const uiContext = winrt::apartment_context{};
            co_await App::Services().Catalog->LoadAsync();
            co_await uiContext;
            if (m_attached)
            {
                RefreshJumpBackIn();
            }
        }
        catch (...)
        {
        }
    }

    void ShellPage::RefreshAccountIdentity()
    {
        auto const session = App::Services().Session;
        if (session->Mode() == ::HaloDesktop::Services::SessionMode::Device)
        {
            // Halo without an account: this PC, with the stock avatar.
            AccountName().Text(winrt::hstring{ ::HaloDesktop::Services::DeviceAccountName });
            AccountRole().Text(winrt::hstring{ ::HaloDesktop::Services::DeviceAccountLabel });
            UpdateAccountToolTip();
            RefreshAccountPictureAsync();
            return;
        }
        auto const plan = session->Plan();
        auto const label = ::HaloDesktop::Services::AccountLabel(::HaloDesktop::Services::AccountLabelInput{
            .IsAdmin = session->IsAdmin(),
            .Plan = plan ? std::wstring_view{ *plan } : std::wstring_view{},
        });
        AccountName().Text(::HaloDesktop::Services::AccountDisplayName(session->UserName()));
        AccountRole().Text(label);
        UpdateAccountToolTip();
        RefreshAccountPictureAsync();
    }

    // Open, the row says what it opens; compact, where only the avatar shows, it
    // names the account.
    void ShellPage::UpdateAccountToolTip()
    {
        auto const name = AccountName().Text();
        Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
            AccountButton(),
            winrt::box_value(m_paneOpen || name.empty() ? winrt::hstring{ AccountToolTip } : name));
    }

    void ShellPage::ShowStockAvatar()
    {
        AccountAvatar().Source(m_stockAvatar);
        m_shownPictureVersion.clear();
        m_shownPictureUser.clear();
    }

    winrt::fire_and_forget ShellPage::RefreshAccountPictureAsync()
    {
        auto lifetime = get_strong();
        auto const request = ++m_pictureRequest;
        auto const session = App::Services().Session;
        auto const userId = session->UserId();
        auto const version = session->IsSignedIn()
            ? session->AvatarVersion().value_or(winrt::hstring{})
            : winrt::hstring{};
        if (version.empty() || userId.empty())
        {
            ShowStockAvatar();
            co_return;
        }
        if (version == m_shownPictureVersion && userId == m_shownPictureUser)
        {
            co_return;
        }
        // Another account's picture never stays up while this one's loads; the
        // same account keeps its old picture until the new one is ready.
        if (userId != m_shownPictureUser)
        {
            ShowStockAvatar();
        }

        auto const uiContext = winrt::apartment_context{};
        try
        {
            auto const bytes = co_await App::Services().AccountPicture->PictureAsync(version);
            co_await uiContext;
            if (request != m_pictureRequest)
            {
                co_return;
            }
            if (!bytes || bytes->empty() || bytes->size() > (std::numeric_limits<std::uint32_t>::max)())
            {
                ShowStockAvatar();
                co_return;
            }

            auto const picture = co_await ::HaloDesktop::Views::DecodeAccountPictureAsync(bytes, AvatarSize);
            co_await uiContext;
            if (request != m_pictureRequest)
            {
                co_return;
            }
            AccountAvatar().Source(picture);
            m_shownPictureVersion = version;
            m_shownPictureUser = userId;
        }
        catch (...)
        {
            // Not an image, or one that could not be read: the stock avatar stands in.
        }
        if (request == m_pictureRequest && m_shownPictureVersion != version)
        {
            ShowStockAvatar();
        }
    }
}
