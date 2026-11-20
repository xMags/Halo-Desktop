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
#include "Services/NavigationService.h"
#include "Shell/LayoutMetricsService.h"
#include "Shell/PointerFocusPolicy.h"
#include "Views/AccountPictureDecoder.h"
#include "Views/PageDialog.h"

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <limits>
#include <string_view>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

namespace
{
    // The avatar's drawn size; the picture is decoded for it rather than at the
    // size the account uploaded.
    constexpr std::uint32_t AvatarSize = 28;
    constexpr wchar_t AccountToolTip[] = L"Server & account";

    // The selection pill's move: it stays on the old item for a beat, then in about 130ms
    // stretches to cover both items and snaps onto the new one.
    constexpr std::chrono::milliseconds IndicatorHold{ 150 };
    constexpr std::chrono::milliseconds IndicatorSpan{ 215 };
    constexpr std::chrono::milliseconds IndicatorLand{ 280 };
    constexpr winrt::Windows::Foundation::Point StretchFirst{ 0.5F, 0.0F };
    constexpr winrt::Windows::Foundation::Point StretchSecond{ 0.9F, 0.5F };
    constexpr winrt::Windows::Foundation::Point SnapFirst{ 0.1F, 0.6F };
    constexpr winrt::Windows::Foundation::Point SnapSecond{ 0.3F, 1.0F };

    bool AnimationsEnabled()
    {
        try
        {
            return winrt::Windows::UI::ViewManagement::UISettings{}.AnimationsEnabled();
        }
        catch (winrt::hresult_error const&)
        {
            return false;
        }
    }

    winrt::Microsoft::UI::Xaml::Media::Animation::KeyTime KeyAt(std::chrono::milliseconds time)
    {
        return winrt::Microsoft::UI::Xaml::Media::Animation::KeyTimeHelper::FromTimeSpan(
            std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(time));
    }

    // The pill's three phases for one property: `rest` through the hold, then
    // `span` by IndicatorSpan (accelerating), then `land` by IndicatorLand
    // (decelerating). Both properties use the same curves in each phase, which
    // is what keeps the pill's far edge still while it stretches and its near
    // edge still while it snaps.
    winrt::Microsoft::UI::Xaml::Media::Animation::DoubleAnimationUsingKeyFrames PillTrack(
        double rest,
        double span,
        double land)
    {
        namespace Animation = winrt::Microsoft::UI::Xaml::Media::Animation;
        Animation::DoubleAnimationUsingKeyFrames track;
        Animation::DiscreteDoubleKeyFrame start;
        start.KeyTime(KeyAt({}));
        start.Value(rest);
        track.KeyFrames().Append(start);
        Animation::DiscreteDoubleKeyFrame hold;
        hold.KeyTime(KeyAt(IndicatorHold));
        hold.Value(rest);
        track.KeyFrames().Append(hold);
        Animation::SplineDoubleKeyFrame stretch;
        stretch.KeyTime(KeyAt(IndicatorSpan));
        stretch.Value(span);
        Animation::KeySpline stretchCurve;
        stretchCurve.ControlPoint1(StretchFirst);
        stretchCurve.ControlPoint2(StretchSecond);
        stretch.KeySpline(stretchCurve);
        track.KeyFrames().Append(stretch);
        Animation::SplineDoubleKeyFrame snap;
        snap.KeyTime(KeyAt(IndicatorLand));
        snap.Value(land);
        Animation::KeySpline snapCurve;
        snapCurve.ControlPoint1(SnapFirst);
        snapCurve.ControlPoint2(SnapSecond);
        snap.KeySpline(snapCurve);
        track.KeyFrames().Append(snap);
        return track;
    }

    // IsChecked is nullable (a three-state box); the rail's are never indeterminate.
    bool IsLit(winrt::Microsoft::UI::Xaml::Controls::RadioButton const& item)
    {
        auto const checked = item.IsChecked();
        return checked && checked.Value();
    }
}

namespace winrt::HaloDesktop::implementation
{
    ShellPage::ShellPage() = default;

    void ShellPage::InitializeComponent()
    {
        ShellPageT::InitializeComponent();
        m_stockAvatar = AccountAvatar().Source();
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
        UpdateDownloadBadge();
        RefreshAccountIdentity();
        // Also picks up a notice still pending from before the shell last unloaded.
        ShowSkippedAddons(App::Services().DeviceMove->TakeUnreportedSkippedAddons());
    }

    void ShellPage::OnIdentityChanged()
    {
        RefreshAccountIdentity();
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
        // listens on the whole shell rather than the frame host: a press over the
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
        ShellRoot().AddHandler(
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
                ShellRoot().RemoveHandler(
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

    void ShellPage::OnRailItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const item = sender.try_as<Microsoft::UI::Xaml::FrameworkElement>();
        if (!item)
        {
            return;
        }
        NavigateFromTag(winrt::unbox_value_or<winrt::hstring>(item.Tag(), L""));
        // A click lights the item at once. When nothing navigates (a press on
        // the page already shown), the frame raises nothing, so the rail is
        // put back to the page that is up.
        UpdateNavigationState(App::Services().Navigation->CurrentPage());
    }

    void ShellPage::OnSearchAcceleratorInvoked(
        [[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        // The shortcut opens the search page, which focuses its own input on arrival.
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
    // rail subtracted.
    void ShellPage::OnContentHostSizeChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        App::Services().LayoutMetrics->SetContentWidth(args.NewSize().Width);
    }

    void ShellPage::OnFrameNavigated(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args)
    {
        UpdateNavigationState(App::Services().Navigation->CurrentPage());
    }

    Microsoft::UI::Xaml::Controls::Frame ShellPage::ContentFrameControl() const
    {
        return FindName(L"ContentFrame").as<Microsoft::UI::Xaml::Controls::Frame>();
    }

    // The rail's lit destination, or null when none is.
    Microsoft::UI::Xaml::Controls::RadioButton ShellPage::CheckedRailItem()
    {
        for (auto const& item : { HomeItem(), SearchItem(), LibraryItem(), HistoryItem(), DownloadsItem(), SettingsItem() })
        {
            if (IsLit(item))
            {
                return item;
            }
        }
        return nullptr;
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
        if (section == Page::Detail || section == Page::Person || section == Page::Sources)
        {
            section = App::Services().Navigation->SectionBehindCurrentPage();
        }
        Microsoft::UI::Xaml::Controls::RadioButton selectedItem{ nullptr };
        switch (section)
        {
        case Page::Home:
        case Page::Detail:
        case Page::Person:
            selectedItem = HomeItem();
            break;
        case Page::Search:
            selectedItem = SearchItem();
            break;
        case Page::Catalog:
            // A See all page keeps whichever section it was opened from lit.
            selectedItem = CheckedRailItem();
            if (!selectedItem)
            {
                selectedItem = HomeItem();
            }
            break;
        case Page::Library:
            selectedItem = LibraryItem();
            break;
        case Page::History:
            selectedItem = HistoryItem();
            break;
        case Page::Downloads:
            selectedItem = DownloadsItem();
            break;
        case Page::Settings:
            selectedItem = SettingsItem();
            break;
        }

        // The group unlights the others; Checked starts no navigation (only Click does).
        if (selectedItem && !IsLit(selectedItem))
        {
            selectedItem.IsChecked(true);
        }
        MoveRailIndicator(selectedItem);
    }

    // The pill's place is worked out from the item's index rather than its laid-out
    // position, so the first placement (before the rail's first layout) is right too:
    // every item is the style's fixed height, RailItems' spacing apart, never hidden.
    void ShellPage::MoveRailIndicator(Microsoft::UI::Xaml::Controls::RadioButton const& item)
    {
        namespace Animation = Microsoft::UI::Xaml::Media::Animation;
        auto const indicator = RailIndicator();
        auto const transform = RailIndicatorTransform();
        auto const items = RailItems();
        std::uint32_t index{};
        if (!item || !items.Children().IndexOf(item, index))
        {
            indicator.Opacity(0.0);
            return;
        }
        auto const pitch = item.Height() + items.Spacing();
        auto const target = static_cast<double>(index) * pitch + (item.Height() - indicator.Height()) / 2.0;
        // Read before stopping: a pill caught mid-slide carries on from where it is.
        auto const from = transform.TranslateY();
        auto const wasShown = indicator.Opacity() > 0.0;
        if (m_indicatorSlide)
        {
            m_indicatorSlide.Stop();
            m_indicatorSlide = nullptr;
        }
        transform.TranslateY(target);
        transform.ScaleY(1.0);
        indicator.Opacity(1.0);
        if (!wasShown || std::abs(from - target) < 0.5 || !AnimationsEnabled())
        {
            return;
        }

        // Scaled from its top edge. At full stretch it runs from the higher
        // item's pill to the lower one's: moving down its top stays put while
        // it grows, moving up its bottom does.
        auto const length = indicator.Height();
        auto const span = (std::abs(target - from) + length) / length;
        auto const spanTop = (std::min)(from, target);
        auto const move = PillTrack(from, spanTop, target);
        Animation::Storyboard::SetTarget(move, transform);
        Animation::Storyboard::SetTargetProperty(move, L"TranslateY");
        auto const scale = PillTrack(1.0, span, 1.0);
        Animation::Storyboard::SetTarget(scale, transform);
        Animation::Storyboard::SetTargetProperty(scale, L"ScaleY");
        Animation::Storyboard slide;
        slide.Children().Append(move);
        slide.Children().Append(scale);
        try
        {
            slide.Begin();
            m_indicatorSlide = slide;
        }
        catch (winrt::hresult_error const&)
        {
            // Unanimated, it is already where it belongs.
        }
    }

    void ShellPage::UpdateDownloadBadge()
    {
        auto const count = App::Services().Downloads->ActiveCount();
        // A dot on the icon: a number would crowd the glyph.
        DownloadsDot().Visibility(count != 0
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed);
    }

    void ShellPage::RefreshAccountIdentity()
    {
        auto const session = App::Services().Session;
        if (session->Mode() == ::HaloDesktop::Services::SessionMode::Device)
        {
            // Halo without an account: this PC, with the stock avatar.
            m_accountName = winrt::hstring{ ::HaloDesktop::Services::DeviceAccountName };
            m_accountRole = winrt::hstring{ ::HaloDesktop::Services::DeviceAccountLabel };
            UpdateAccountToolTip();
            RefreshAccountPictureAsync();
            return;
        }
        auto const plan = session->Plan();
        auto const label = ::HaloDesktop::Services::AccountLabel(::HaloDesktop::Services::AccountLabelInput{
            .IsAdmin = session->IsAdmin(),
            .Plan = plan ? std::wstring_view{ *plan } : std::wstring_view{},
        });
        m_accountName = winrt::hstring{ ::HaloDesktop::Services::AccountDisplayName(session->UserName()) };
        m_accountRole = winrt::hstring{ label };
        UpdateAccountToolTip();
        RefreshAccountPictureAsync();
    }

    // Only the avatar shows, so the tooltip names the account and its tier (what
    // the expanded pane used to print beside it). A screen reader hears the name
    // and what the button opens.
    void ShellPage::UpdateAccountToolTip()
    {
        auto const name = m_accountName;
        auto const tip = name.empty()
            ? winrt::hstring{ AccountToolTip }
            : (m_accountRole.empty() ? name : name + L" \u00B7 " + m_accountRole);
        Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(AccountButton(), winrt::box_value(tip));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
            AccountButton(),
            name.empty() ? winrt::hstring{ AccountToolTip } : name + L", " + AccountToolTip);
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

            auto const picture = co_await ::HaloDesktop::Views::DecodeAccountPictureAsync(bytes, AvatarSize, ::HaloDesktop::Views::RasterizationScaleOf(*this));
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
