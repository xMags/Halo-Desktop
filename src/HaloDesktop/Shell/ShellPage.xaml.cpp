#include "pch.h"
#include "Shell/ShellPage.xaml.h"
#if __has_include("ShellPage.g.cpp")
#include "ShellPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Shell/LayoutMetricsService.h"

#include <array>
#include <winrt/Microsoft.UI.Input.h>

namespace winrt::HaloDesktop::implementation
{
    ShellPage::ShellPage() = default;

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
        UpdateDownloadBadge();
        RefreshAccountIdentity();
        RefreshJumpBackIn();
        RefreshJumpBackInAsync();
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
        SetJumpBackVisibility(true);
    }

    void ShellPage::OnPaneClosing(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::NavigationView const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::NavigationViewPaneClosingEventArgs const& args)
    {
        SetJumpBackVisibility(false);
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
                App::Services().Navigation->GoTo(Page::Sources, m_jumpItems[index]);
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

        Microsoft::UI::Xaml::Controls::NavigationViewItem selectedItem{ nullptr };
        switch (page)
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
        auto const uiContext = winrt::apartment_context{};
        try
        {
            co_await App::Services().Catalog->LoadAsync();
        }
        catch (...)
        {
            co_return;
        }
        co_await uiContext;
        if (m_attached)
        {
            RefreshJumpBackIn();
        }
    }

    void ShellPage::RefreshAccountIdentity()
    {
        auto const session = App::Services().Session;
        FindName(L"AccountName").as<Microsoft::UI::Xaml::Controls::TextBlock>().Text(session->UserName());
        // Placeholder until the server reports a plan: every signed-in account reads as
        // premium. The admin flag no longer shows here, but the settings account card
        // still carries it.
        FindName(L"AccountRole").as<Microsoft::UI::Xaml::Controls::TextBlock>().Text(L"PREMIUM");
    }
}
