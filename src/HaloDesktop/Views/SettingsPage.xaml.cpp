#include "pch.h"
#include "Views/SettingsPage.xaml.h"
#include "Controls/SmoothWheelScroll.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

#include "App.xaml.h"
#include "Services/AccountPictureService.h"
#include "Views/AccountPictureDecoder.h"
#include "Views/PageDialog.h"
#include "ViewModels/SettingsRailPolicy.h"
#include "ViewModels/SettingsViewModel.h"
#include "ViewModels/TmdbKeyViewModel.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.System.h>

namespace
{
    // The client's own repository. Releases are where a newer build would appear, so
    // that is the page the card offers rather than the repository root.
    constexpr wchar_t const* kReleasesUrl = L"https://github.com/xMags/Halo-Desktop/releases";

    // The rail entries in document order, paired with the section each one points at
    // and the visual state that lights it. The three arrays are indexed together, so
    // they must stay in the same order as the rail itself.
    constexpr std::array<wchar_t const*, 5> kSectionNames{
        L"AccountSection",
        L"AppearanceSection",
        L"PlaybackSection",
        L"SubtitlesSection",
        L"AddonsSection",
    };
    constexpr std::array<wchar_t const*, 5> kRailStates{
        L"RailAccount",
        L"RailAppearance",
        L"RailPlayback",
        L"RailSubtitles",
        L"RailAddons",
    };

    // How far below the top edge a section has to reach before the rail counts it as
    // the one being read. A band rather than the edge itself, so a boundary resting on
    // the top of the viewport does not flip the selection back and forth.
    constexpr double kAnchorFraction = 0.25;

    // The account card's avatar is drawn 64 pixels square.
    constexpr std::uint32_t kAccountPictureEdge = 64;
}

namespace winrt::HaloDesktop::implementation
{
    SettingsPage::SettingsPage()
        : m_viewModel(winrt::make<SettingsViewModel>(App::Services())),
          m_tmdbKey(winrt::make<TmdbKeyViewModel>(App::Services().TmdbKey))
    {
        winrt::get_self<TmdbKeyViewModel>(m_tmdbKey)->Attach();
    }

    void SettingsPage::OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args)
    {
        auto const section = winrt::unbox_value_or<winrt::hstring>(args.Parameter(), L"");
        m_showAddAddon = section == L"addons";
        // A page already on screen gets no Loaded: it is shown from here.
        if (m_showAddAddon && m_loaded) ShowAddAddonWhenLaidOut();
    }

    // Queued behind layout, so the section's place is known, and behind the
    // navigation's own focus change, so the caret stays in the box.
    void SettingsPage::ShowAddAddonWhenLaidOut()
    {
        DispatcherQueue().TryEnqueue(
            Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [weak = get_weak()]()
            {
                auto const self = weak.get();
                if (!self || !self->m_showAddAddon) return;
                self->m_showAddAddon = false;
                try
                {
                    self->ScrollTo(L"AddonsSection");
                    self->AddonUrlBox().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
                }
                catch (winrt::hresult_error const&)
                {
                }
            });
    }

    void SettingsPage::OnSaveTmdbKeyClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_tmdbKey.Save();
    }

    void SettingsPage::OnRemoveTmdbKeyClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_tmdbKey.Remove();
    }

    // Enter in the key box saves, as the addon box adds.
    void SettingsPage::OnTmdbKeyKeyDown(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() != winrt::Windows::System::VirtualKey::Enter) return;
        args.Handled(true);
        m_tmdbKey.Save();
    }
    winrt::HaloDesktop::SettingsViewModel SettingsPage::ViewModel() const { return m_viewModel; }
    // Bound to each switch's own IsOn, so the word always matches what the switch shows.
    winrt::hstring SettingsPage::OnOffText(bool isOn) { return isOn ? winrt::hstring{ L"On" } : winrt::hstring{ L"Off" }; }
    void SettingsPage::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ::HaloDesktop::Controls::AttachSmoothWheelScroll(SettingsScroller());
        if (!m_stockAccountPicture)
        {
            m_stockAccountPicture = AccountPicture().Source();
        }
        if (!m_viewModelChangedRevoker)
        {
            // The view model owns the account's identity and says when it moved;
            // the picture itself is an image, so the page draws it. Weak, because
            // the page owns the view model. Kept across Unloaded: the page is cached.
            m_viewModelChangedRevoker = m_viewModel.PropertyChanged(winrt::auto_revoke,
                [weak = get_weak()](auto const&, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args)
            {
                if (args.PropertyName() != L"AccountPicture")
                {
                    return;
                }
                if (auto const self = weak.get(); self && self->m_loaded)
                {
                    self->RefreshAccountPictureAsync();
                }
            });
        }
        m_loaded = true;
        if (m_showAddAddon) ShowAddAddonWhenLaidOut();
        m_viewModel.Refresh();
        RefreshAccountPictureAsync();
        if (!m_healthTimer)
        {
            m_healthTimer = DispatcherQueue().CreateTimer();
            m_healthTimer.Interval(std::chrono::seconds{ 30 });
            m_healthTimer.IsRepeating(true);
            m_healthTickRevoker = m_healthTimer.Tick(winrt::auto_revoke, [weak = get_weak()](auto const&, auto const&)
            {
                if (auto const self = weak.get())
                {
                    self->m_viewModel.ProbeHealth();
                }
            });
        }
        m_healthTimer.Start();
        // The rail follows the scroll position from here on, but the page opens at the
        // top, so light the first entry before anything has scrolled.
        UpdateRailSelection();
    }
    void SettingsPage::OnUnloaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_loaded = false;
        if (m_healthTimer)
        {
            m_healthTimer.Stop();
        }
        m_healthTickRevoker.revoke();
        m_healthTimer = nullptr;
        m_viewModel.CancelHealthProbe();
    }
    void SettingsPage::OnAppearanceRailClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ScrollTo(L"AppearanceSection"); }
    void SettingsPage::OnLightThemeClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetTheme(0); }
    void SettingsPage::OnDarkThemeClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetTheme(1); }
    void SettingsPage::OnSystemThemeClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetTheme(2); }
    void SettingsPage::OnAddonsRailClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ScrollTo(L"AddonsSection"); }
    void SettingsPage::OnPlaybackRailClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ScrollTo(L"PlaybackSection"); }
    void SettingsPage::OnSubtitlesRailClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ScrollTo(L"SubtitlesSection"); }
    void SettingsPage::OnAccountRailClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ScrollTo(L"AccountSection"); }
    void SettingsPage::OnAddAddonClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_viewModel.AddAddon();
    }
    void SettingsPage::OnAddonUrlKeyDown([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (args.Key() != winrt::Windows::System::VirtualKey::Enter)
        {
            return;
        }
        args.Handled(true);
        m_viewModel.AddAddon();
    }
    void SettingsPage::OnRetrySettingsClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.Refresh(); }
    void SettingsPage::OnDeleteAddonClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ShowDeleteAddonDialog(winrt::unbox_value<winrt::hstring>(sender.as<Microsoft::UI::Xaml::Controls::Button>().Tag()));
    }
    void SettingsPage::OnInstallForEveryoneClick(winrt::Windows::Foundation::IInspectable const& sender, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ShowInstallForEveryoneDialog(winrt::unbox_value<winrt::hstring>(sender.as<Microsoft::UI::Xaml::Controls::Button>().Tag()));
    }
    void SettingsPage::OnAddonDragStarting([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::DragItemsStartingEventArgs const& args)
    {
        // A shadowed copy has no order that means anything, and only one save
        // runs at a time, so such a drag never starts.
        try
        {
            std::vector<winrt::hstring> ids;
            for (auto const& item : args.Items())
            {
                if (auto const row = item.try_as<winrt::HaloDesktop::AddonRowViewModel>())
                {
                    ids.push_back(row.Id());
                }
            }
            if (!winrt::get_self<SettingsViewModel>(m_viewModel)->CanDragRows(ids))
            {
                args.Cancel(true);
            }
        }
        catch (winrt::hresult_error const&)
        {
            args.Cancel(true);
        }
    }
    void SettingsPage::OnAddonDragCompleted(Microsoft::UI::Xaml::Controls::ListViewBase const& sender, Microsoft::UI::Xaml::Controls::DragItemsCompletedEventArgs const& args)
    {
        if (args.DropResult() != winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Move)
        {
            return;
        }
        // The list has already moved the row in the view model's collection;
        // the view model saves that order, or puts the rows back.
        m_viewModel.CommitReorder(sender == GlobalAddonList());
    }
    void SettingsPage::OnAddonRowContextRequested(Microsoft::UI::Xaml::UIElement const& sender, Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const& args)
    {
        // The row menu is how a keyboard reaches the order: the context menu key
        // or Shift+F10 on a focused row offers Move up and Move down.
        try
        {
            auto const list = sender.as<Microsoft::UI::Xaml::Controls::ListView>();
            Microsoft::UI::Xaml::Controls::ListViewItem container{ nullptr };
            auto element = args.OriginalSource().try_as<Microsoft::UI::Xaml::DependencyObject>();
            while (element)
            {
                container = element.try_as<Microsoft::UI::Xaml::Controls::ListViewItem>();
                if (container || element == list)
                {
                    break;
                }
                element = Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(element);
            }
            if (!container)
            {
                return;
            }
            auto const row = list.ItemFromContainer(container).try_as<winrt::HaloDesktop::AddonRowViewModel>();
            if (!row)
            {
                return;
            }
            auto const id = row.Id();
            auto const moves = winrt::get_self<SettingsViewModel>(m_viewModel)->MovesFor(id);
            if (!moves.Offered)
            {
                return;
            }
            args.Handled(true);

            Microsoft::UI::Xaml::Controls::MenuFlyout menu;
            auto const addMove = [&menu, weak = get_weak(), id](wchar_t const* text, wchar_t const* glyph, std::int32_t offset, bool enabled)
            {
                Microsoft::UI::Xaml::Controls::MenuFlyoutItem item;
                item.Text(text);
                Microsoft::UI::Xaml::Controls::FontIcon icon;
                icon.Glyph(glyph);
                item.Icon(icon);
                item.IsEnabled(enabled);
                item.Click([weak, id, offset](auto const&, auto const&)
                {
                    if (auto const self = weak.get())
                    {
                        self->m_viewModel.MoveAddon(id, offset);
                    }
                });
                menu.Items().Append(item);
            };
            addMove(L"Move up", L"\xE74A", -1, moves.CanMoveUp);
            addMove(L"Move down", L"\xE74B", 1, moves.CanMoveDown);

            Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowOptions options;
            winrt::Windows::Foundation::Point point{};
            if (args.TryGetPosition(container, point))
            {
                options.Position(point);
            }
            menu.ShowAt(container, options);
        }
        catch (winrt::hresult_error const&)
        {
        }
    }
    void SettingsPage::OnDefaultFontClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFont(0); }
    void SettingsPage::OnSystemFontClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFont(1); }
    void SettingsPage::OnSerifFontClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFont(2); }
    void SettingsPage::OnMonoFontClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetFont(3); }
    void SettingsPage::OnNoOutlineClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetOutline(0); }
    void SettingsPage::OnThinOutlineClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetOutline(1); }
    void SettingsPage::OnNormalOutlineClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetOutline(2); }
    void SettingsPage::OnHeavyOutlineClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SetOutline(3); }
    void SettingsPage::OnCheckForUpdatesClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        OpenReleasesPageAsync();
    }

    winrt::fire_and_forget SettingsPage::OpenReleasesPageAsync()
    {
        auto lifetime = get_strong();
        try
        {
            bool launched{};
            try
            {
                launched = co_await winrt::Windows::System::Launcher::LaunchUriAsync(
                    winrt::Windows::Foundation::Uri{ kReleasesUrl });
            }
            catch (...)
            {
            }
            if (launched)
            {
                co_return;
            }
            auto const xamlRoot = XamlRoot();
            if (!xamlRoot)
            {
                co_return;
            }
            auto dialog = ::HaloDesktop::Views::MakeDialog(
                xamlRoot,
                ActualTheme(),
                L"Could not open the releases page",
                L"Check your default browser settings and try again.");
            dialog.CloseButtonText(L"Close");
            co_await dialog.ShowAsync();
        }
        catch (...)
        {
        }
    }
    void SettingsPage::OnSignOutClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { ShowSignOutDialog(); }
    void SettingsPage::OnSignInToSyncClick([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&) { m_viewModel.SignInToSync(); }
    void SettingsPage::ScrollTo(wchar_t const* elementName)
    {
        auto const scroller = SettingsScroller();
        auto const section = FindName(elementName).as<Microsoft::UI::Xaml::FrameworkElement>();
        auto const top = section.TransformToVisual(scroller).TransformPoint({ 0.0f, 0.0f }).Y;
        auto const targetOffset = scroller.VerticalOffset() + top;
        scroller.ScrollTo(
            scroller.HorizontalOffset(),
            targetOffset,
            Microsoft::UI::Xaml::Controls::ScrollingScrollOptions{ Microsoft::UI::Xaml::Controls::ScrollingAnimationMode::Disabled });
    }
    void SettingsPage::OnFormViewChanged([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&)
    {
        UpdateRailSelection();
    }
    void SettingsPage::OnFormSizeChanged([[maybe_unused]] winrt::Windows::Foundation::IInspectable const&, [[maybe_unused]] Microsoft::UI::Xaml::SizeChangedEventArgs const&)
    {
        // Sections shift when the addon list finishes loading, which moves the reader
        // to a different section without any scrolling having happened.
        UpdateRailSelection();
    }
    void SettingsPage::UpdateRailSelection()
    {
        if (!m_loaded)
        {
            return;
        }
        auto const scroller = SettingsScroller();
        auto const viewport = scroller.ViewportHeight();
        if (viewport <= 0.0)
        {
            return;
        }
        std::array<double, kSectionNames.size()> tops{};
        for (std::size_t index = 0; index < kSectionNames.size(); ++index)
        {
            auto const section = FindName(kSectionNames[index]).try_as<Microsoft::UI::Xaml::FrameworkElement>();
            if (!section)
            {
                return;
            }
            tops[index] = section.TransformToVisual(scroller).TransformPoint({ 0.0f, 0.0f }).Y;
        }
        // Only treat the view as ended when there was somewhere to scroll to, otherwise
        // a form short enough to fit would sit permanently on its last section.
        auto const scrollable = scroller.ScrollableHeight();
        auto const atEnd = scrollable > 0.5 && scroller.VerticalOffset() >= scrollable - 1.0;
        auto const active = ::HaloDesktop::ViewModels::ActiveSettingsSection(tops, viewport * kAnchorFraction, atEnd);
        if (m_railApplied && active == m_activeRail)
        {
            return;
        }
        m_activeRail = active;
        m_railApplied = true;
        Microsoft::UI::Xaml::VisualStateManager::GoToState(*this, kRailStates[active], false);
    }
    Microsoft::UI::Xaml::Controls::ContentDialog SettingsPage::MakeConfirmation(::HaloDesktop::Services::ConfirmationCopy const& copy)
    {
        auto dialog = ::HaloDesktop::Views::MakeDialog(
            XamlRoot(),
            ActualTheme(),
            winrt::hstring{ copy.Title },
            winrt::hstring{ copy.Body },
            copy.Destructive ? ::HaloDesktop::Views::DialogTone::Destructive : ::HaloDesktop::Views::DialogTone::Standard);
        dialog.PrimaryButtonText(winrt::hstring{ copy.Primary });
        dialog.CloseButtonText(winrt::hstring{ copy.Close });
        return dialog;
    }

    winrt::fire_and_forget SettingsPage::ShowDeleteAddonDialog(winrt::hstring id)
    {
        auto lifetime = get_strong();
        try
        {
            auto const copy = winrt::get_self<SettingsViewModel>(m_viewModel)->RemoveConfirmation(id);
            if (!copy)
            {
                co_return;
            }
            auto dialog = MakeConfirmation(*copy);
            if (co_await dialog.ShowAsync() == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
            {
                m_viewModel.RemoveAddon(id);
            }
        }
        catch (...)
        {
        }
    }

    winrt::fire_and_forget SettingsPage::ShowInstallForEveryoneDialog(winrt::hstring id)
    {
        auto lifetime = get_strong();
        try
        {
            // Checked before asking: a global list read without its URLs cannot
            // be sent back whole, and says so instead of asking.
            auto const copy = winrt::get_self<SettingsViewModel>(m_viewModel)->BeginInstallForEveryone(id);
            if (!copy)
            {
                co_return;
            }
            auto dialog = MakeConfirmation(*copy);
            if (co_await dialog.ShowAsync() == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
            {
                m_viewModel.InstallForEveryone(id);
            }
        }
        catch (...)
        {
        }
    }

    winrt::fire_and_forget SettingsPage::ShowSignOutDialog()
    {
        auto lifetime = get_strong();
        try
        {
            auto dialog = MakeConfirmation(::HaloDesktop::Services::SignOutConfirmation());
            if (co_await dialog.ShowAsync() == Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
            {
                m_viewModel.SignOut();
            }
        }
        catch (...)
        {
        }
    }

    void SettingsPage::ShowStockAccountPicture()
    {
        if (m_stockAccountPicture)
        {
            AccountPicture().Source(m_stockAccountPicture);
        }
        m_shownPictureVersion.clear();
        m_shownPictureUser.clear();
    }

    // The same picture, from the same cache, as the shell's account button.
    winrt::fire_and_forget SettingsPage::RefreshAccountPictureAsync()
    {
        auto lifetime = get_strong();
        auto const request = ++m_pictureRequest;
        auto const viewModel = winrt::get_self<SettingsViewModel>(m_viewModel);
        auto const userId = viewModel->AccountUserId();
        auto const version = viewModel->AccountPictureVersion();
        if (version.empty() || userId.empty())
        {
            ShowStockAccountPicture();
            co_return;
        }
        if (version == m_shownPictureVersion && userId == m_shownPictureUser)
        {
            co_return;
        }
        // Another account's picture never stays up while this one's loads.
        if (userId != m_shownPictureUser)
        {
            ShowStockAccountPicture();
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
            if (!bytes || bytes->empty())
            {
                ShowStockAccountPicture();
                co_return;
            }
            auto const picture = co_await ::HaloDesktop::Views::DecodeAccountPictureAsync(bytes, kAccountPictureEdge, ::HaloDesktop::Views::RasterizationScaleOf(*this));
            co_await uiContext;
            if (request != m_pictureRequest)
            {
                co_return;
            }
            AccountPicture().Source(picture);
            m_shownPictureVersion = version;
            m_shownPictureUser = userId;
        }
        catch (...)
        {
            // Not an image, or one that could not be read: the stock avatar stands in.
        }
        if (request == m_pictureRequest && m_shownPictureVersion != version)
        {
            ShowStockAccountPicture();
        }
    }
}
