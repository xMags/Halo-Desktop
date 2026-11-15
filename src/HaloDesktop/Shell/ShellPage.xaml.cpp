#include "pch.h"
#include "Shell/ShellPage.xaml.h"
#if __has_include("ShellPage.g.cpp")
#include "ShellPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Services/AccountLabelPolicy.h"
#include "Services/AccountPictureService.h"
#include "Services/DevicePreferencesStore.h"
#include "Services/NavigationService.h"
#include "Shell/LayoutMetricsService.h"

#include <algorithm>
#include <array>
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
    // The compact rail is wider than the pane toggle's own column, so the toggle
    // moves this far right to sit centred over the icons below it.
    constexpr double CompactToggleOffset = 4.0;
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
        // first tab stop in the page. On Home that is the header search box, so a
        // click on empty background put a caret there and dragged the scroll up to
        // show it. Nothing can undo that afterwards, because anything this shell does
        // during the press is what gets overridden, so the move is refused instead.
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
                if (!target || !IsHomeSearchTarget(target))
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
        UpdatePaneToggleOffset();
        UpdateDownloadBadge();
        RefreshAccountIdentity();
        RefreshJumpBackIn();
        RefreshJumpBackInAsync();
    }

    void ShellPage::OnIdentityChanged()
    {
        RefreshAccountIdentity();
        // The account's services were reset before this runs, so this empties the
        // rows of the account that left; the load refills them for the new one.
        RefreshJumpBackIn();
        if (App::Services().Session->IsSignedIn())
        {
            RefreshJumpBackInAsync();
        }
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
        // A picture still loading has nowhere to go.
        ++m_pictureRequest;
        if (m_gettingFocusToken)
        {
            Microsoft::UI::Xaml::Input::FocusManager::GettingFocus(m_gettingFocusToken);
            m_gettingFocusToken = {};
        }
        DetachPointerHandlers();
        m_frameNavigatedRevoker.revoke();
        m_attached = false;
    }

    bool ShellPage::IsWithinTextInput(Microsoft::UI::Xaml::DependencyObject const& element)
    {
        auto current = element;
        while (current)
        {
            if (current.try_as<Microsoft::UI::Xaml::Controls::TextBox>() ||
                current.try_as<Microsoft::UI::Xaml::Controls::AutoSuggestBox>() ||
                current.try_as<Microsoft::UI::Xaml::Controls::RichEditBox>() ||
                current.try_as<Microsoft::UI::Xaml::Controls::PasswordBox>())
            {
                return true;
            }
            current = Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(current);
        }
        return false;
    }

    bool ShellPage::IsHomeSearchTarget(Microsoft::UI::Xaml::DependencyObject const& element)
    {
        auto current = element;
        while (current)
        {
            if (auto const search = current.try_as<Microsoft::UI::Xaml::Controls::AutoSuggestBox>();
                search && search.Name() == L"HomeSearchBox")
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
        // A press that landed inside a text box is that box's business, not ours.
        auto const source = args.OriginalSource().try_as<Microsoft::UI::Xaml::DependencyObject>();
        if (source && IsWithinTextInput(source))
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
        auto const focused =
            Microsoft::UI::Xaml::Input::FocusManager::GetFocusedElement(XamlRoot())
                .try_as<Microsoft::UI::Xaml::DependencyObject>();
        if (focused && IsWithinTextInput(focused))
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
        RememberPaneState(true);
    }

    void ShellPage::OnPaneClosing(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::NavigationView const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::NavigationViewPaneClosingEventArgs const& args)
    {
        ApplyPaneState(false);
        RememberPaneState(false);
    }

    void ShellPage::ApplyPaneState(bool open)
    {
        SetJumpBackVisibility(open);
        AccountLines().Visibility(open
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed);
        UpdateAccountToolTip();
        UpdatePaneToggleOffset();
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
        toggle.Margin(Microsoft::UI::Xaml::Thickness{ m_paneOpen ? 0.0 : CompactToggleOffset, 0.0, 0.0, 0.0 });
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
        // reference does.
        auto const section = page == Page::Detail
            ? App::Services().Navigation->SectionBehindCurrentPage()
            : page;
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
        case Page::Sources:
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
        badge.Visibility(count == 0
            ? Microsoft::UI::Xaml::Visibility::Collapsed
            : Microsoft::UI::Xaml::Visibility::Visible);
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

            winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
            {
                winrt::Windows::Storage::Streams::DataWriter writer{ stream };
                writer.WriteBytes(winrt::array_view<std::uint8_t const>{
                    bytes->data(),
                    static_cast<std::uint32_t>(bytes->size()) });
                co_await writer.StoreAsync();
                writer.DetachStream();
            }

            // The shape is read first so the shorter side is decoded at the
            // avatar's size: an upload can be far larger than the 28 pixels drawn,
            // and decoding both sides would stretch it.
            stream.Seek(0);
            auto const decoder = co_await winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(stream);
            auto const width = decoder.PixelWidth();
            auto const height = decoder.PixelHeight();
            stream.Seek(0);

            Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
            bitmap.DecodePixelType(Microsoft::UI::Xaml::Media::Imaging::DecodePixelType::Logical);
            if (width > 0 && height > 0)
            {
                if (width <= height)
                {
                    bitmap.DecodePixelWidth(static_cast<std::int32_t>((std::min)(width, AvatarSize)));
                }
                else
                {
                    bitmap.DecodePixelHeight(static_cast<std::int32_t>((std::min)(height, AvatarSize)));
                }
            }
            co_await bitmap.SetSourceAsync(stream);
            co_await uiContext;
            if (request != m_pictureRequest)
            {
                co_return;
            }
            AccountAvatar().Source(bitmap);
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
