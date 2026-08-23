#include "pch.h"
#include "Shell/ShellPage.xaml.h"
#if __has_include("ShellPage.g.cpp")
#include "ShellPage.g.cpp"
#endif

#include "App.xaml.h"

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
        m_frameNavigatedRevoker.revoke();
        m_attached = false;
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

    void ShellPage::OnSearchSubmitted(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
        Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const& args)
    {
        App::Services().Navigation->GoTo(
            ::HaloDesktop::Services::Page::Search,
            winrt::box_value(args.QueryText()));
    }

    void ShellPage::OnSearchAcceleratorInvoked(
        [[maybe_unused]] Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
        Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        SearchBoxControl().Focus(Microsoft::UI::Xaml::FocusState::Keyboard);
        args.Handled(true);
    }

    void ShellPage::OnAccountClick(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        App::Services().Navigation->GoTo(::HaloDesktop::Services::Page::Settings);
    }

    void ShellPage::OnPaneOpening(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::NavigationView const& sender,
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args)
    {
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
        auto const visibility = visible
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed;

        JumpHeader().Visibility(visibility);
        JumpItem1().Visibility(visibility);
        JumpItem2().Visibility(visibility);
        JumpItem3().Visibility(visibility);
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

    Microsoft::UI::Xaml::Controls::AutoSuggestBox ShellPage::SearchBoxControl() const
    {
        return FindName(L"SearchBox").as<Microsoft::UI::Xaml::Controls::AutoSuggestBox>();
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
}
