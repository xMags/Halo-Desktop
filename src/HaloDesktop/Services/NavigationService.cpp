#include "pch.h"
#include "Services/NavigationService.h"

#include "Views/CatalogPage.xaml.h"
#include "Views/DetailPage.xaml.h"
#include "Views/DownloadsPage.xaml.h"
#include "Views/HomePage.xaml.h"
#include "Views/LibraryPage.xaml.h"
#include "Views/LoginPage.xaml.h"
#include "Views/PlayerPage.xaml.h"
#include "Views/SearchPage.xaml.h"
#include "Views/SettingsPage.xaml.h"
#include "Views/SourcesPage.xaml.h"

#include <stdexcept>
#include <utility>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

namespace
{
    winrt::Windows::UI::Xaml::Interop::TypeName PageType(HaloDesktop::Services::Page page)
    {
        using HaloDesktop::Services::Page;

        switch (page)
        {
        case Page::Home:
            return winrt::xaml_typename<winrt::HaloDesktop::HomePage>();
        case Page::Search:
            return winrt::xaml_typename<winrt::HaloDesktop::SearchPage>();
        case Page::Library:
            return winrt::xaml_typename<winrt::HaloDesktop::LibraryPage>();
        case Page::Catalog:
            return winrt::xaml_typename<winrt::HaloDesktop::CatalogPage>();
        case Page::Detail:
            return winrt::xaml_typename<winrt::HaloDesktop::DetailPage>();
        case Page::Sources:
            return winrt::xaml_typename<winrt::HaloDesktop::SourcesPage>();
        case Page::Downloads:
            return winrt::xaml_typename<winrt::HaloDesktop::DownloadsPage>();
        case Page::Settings:
            return winrt::xaml_typename<winrt::HaloDesktop::SettingsPage>();
        case Page::Login:
            return winrt::xaml_typename<winrt::HaloDesktop::LoginPage>();
        case Page::Player:
            return winrt::xaml_typename<winrt::HaloDesktop::PlayerPage>();
        }

        throw std::invalid_argument("Unknown shell page");
    }

    bool IsOverlayPage(HaloDesktop::Services::Page page) noexcept
    {
        using HaloDesktop::Services::Page;
        return page == Page::Login || page == Page::Player;
    }

    // Sources is presented as a sheet over whatever the shell is showing, so it
    // is neither a shell destination nor a full-window overlay.
    bool IsSheetPage(HaloDesktop::Services::Page page) noexcept
    {
        return page == HaloDesktop::Services::Page::Sources;
    }

    HaloDesktop::Services::Page PageFromType(winrt::Windows::UI::Xaml::Interop::TypeName const& type)
    {
        using HaloDesktop::Services::Page;

        if (type.Name == PageType(Page::Search).Name)
        {
            return Page::Search;
        }
        if (type.Name == PageType(Page::Library).Name)
        {
            return Page::Library;
        }
        if (type.Name == PageType(Page::Catalog).Name)
        {
            return Page::Catalog;
        }
        if (type.Name == PageType(Page::Detail).Name)
        {
            return Page::Detail;
        }
        if (type.Name == PageType(Page::Sources).Name)
        {
            return Page::Sources;
        }
        if (type.Name == PageType(Page::Downloads).Name)
        {
            return Page::Downloads;
        }
        if (type.Name == PageType(Page::Settings).Name)
        {
            return Page::Settings;
        }

        return Page::Home;
    }
}

namespace HaloDesktop::Services
{
    NavigationService::~NavigationService()
    {
        Detach();
    }

    void NavigationService::AttachShellFrame(winrt::Microsoft::UI::Xaml::Controls::Frame const& frame)
    {
        if (!frame)
        {
            throw std::invalid_argument("A shell frame is required");
        }

        m_shellNavigatedRevoker.revoke();
        m_shellFrame = frame;
        m_shellNavigatedRevoker = m_shellFrame.Navigated(
            winrt::auto_revoke,
            [this](
                winrt::Windows::Foundation::IInspectable const& sender,
                winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args)
            {
                OnNavigated(sender, args);
            });
    }

    void NavigationService::AttachOverlayFrame(winrt::Microsoft::UI::Xaml::Controls::Frame const& frame)
    {
        if (!frame)
        {
            throw std::invalid_argument("An overlay frame is required");
        }

        m_overlayFrame = frame;
        m_overlayFrame.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        m_overlayOpen = false;
    }

    void NavigationService::AttachSheetFrame(winrt::Microsoft::UI::Xaml::Controls::Frame const& frame)
    {
        if (!frame)
        {
            throw std::invalid_argument("A sheet frame is required");
        }

        m_sheetFrame = frame;
        m_sheetFrame.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        m_sheetOpen = false;
    }

    void NavigationService::Detach() noexcept
    {
        m_shellNavigatedRevoker.revoke();
        m_shellFrame = nullptr;
        m_overlayFrame = nullptr;
        m_sheetFrame = nullptr;
        m_overlayOpen = false;
        m_sheetOpen = false;
        m_routeChangedHandler = {};
    }

    bool NavigationService::GoTo(
        Page page,
        winrt::Windows::Foundation::IInspectable const& parameter)
    {
        if (!m_shellFrame || IsOverlayPage(page) || IsSheetPage(page))
        {
            return false;
        }

        auto const targetType = PageType(page);
        if (m_shellFrame.Content() && m_shellFrame.CurrentSourcePageType().Name == targetType.Name && !parameter)
        {
            return false;
        }

        auto const transition = winrt::Microsoft::UI::Xaml::Media::Animation::EntranceNavigationTransitionInfo{};
        return m_shellFrame.Navigate(targetType, parameter, transition);
    }

    bool NavigationService::GoBack()
    {
        if (m_overlayOpen)
        {
            if (!m_overlayFrame || !m_overlayFrame.CanGoBack())
            {
                return false;
            }

            m_overlayFrame.GoBack();
            return true;
        }

        // Back over an open sheet dismisses the sheet rather than moving the shell
        // underneath it, which the user cannot see changing anyway.
        if (m_sheetOpen)
        {
            return CloseSheet();
        }

        if (!CanGoBack())
        {
            return false;
        }

        m_shellFrame.GoBack();
        return true;
    }

    bool NavigationService::CanGoBack() const noexcept
    {
        if (m_overlayOpen)
        {
            return m_overlayFrame && m_overlayFrame.CanGoBack();
        }
        if (m_sheetOpen)
        {
            return true;
        }
        return m_shellFrame && m_shellFrame.CanGoBack();
    }

    bool NavigationService::ShowOverlay(
        Page page,
        winrt::Windows::Foundation::IInspectable const& parameter)
    {
        if (!m_overlayFrame || !IsOverlayPage(page))
        {
            return false;
        }

        auto const targetType = PageType(page);
        auto navigated = true;
        if (!m_overlayFrame.Content() || m_overlayFrame.CurrentSourcePageType().Name != targetType.Name)
        {
            auto const transition = winrt::Microsoft::UI::Xaml::Media::Animation::EntranceNavigationTransitionInfo{};
            navigated = m_overlayFrame.Navigate(
                targetType,
                parameter,
                winrt::Microsoft::UI::Xaml::Media::Animation::EntranceNavigationTransitionInfo{});
        }
        if (!navigated)
        {
            return false;
        }

        m_overlayFrame.BackStack().Clear();
        m_currentOverlayPage = page;
        m_overlayOpen = true;
        m_overlayFrame.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        if (m_routeChangedHandler)
        {
            m_routeChangedHandler(page);
        }
        return true;
    }

    bool NavigationService::CloseOverlay()
    {
        if (!m_overlayFrame || !m_overlayOpen)
        {
            return false;
        }

        m_overlayFrame.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        m_overlayFrame.Content(nullptr);
        m_overlayFrame.BackStack().Clear();
        m_overlayOpen = false;
        if (m_routeChangedHandler)
        {
            m_routeChangedHandler(m_currentPage);
        }
        return true;
    }

    bool NavigationService::ShowSheet(
        Page page,
        winrt::Windows::Foundation::IInspectable const& parameter)
    {
        if (!m_sheetFrame || !IsSheetPage(page))
        {
            return false;
        }

        // Always a fresh navigation: reopening the sheet for a different episode
        // has to reach the page's parameter handling, which reusing content skips.
        auto const navigated = m_sheetFrame.Navigate(
            PageType(page),
            parameter,
            winrt::Microsoft::UI::Xaml::Media::Animation::SuppressNavigationTransitionInfo{});
        if (!navigated)
        {
            return false;
        }

        m_sheetFrame.BackStack().Clear();
        m_sheetOpen = true;
        m_sheetFrame.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        return true;
    }

    bool NavigationService::CloseSheet()
    {
        if (!m_sheetFrame || !m_sheetOpen)
        {
            return false;
        }

        m_sheetFrame.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        m_sheetFrame.Content(nullptr);
        m_sheetFrame.BackStack().Clear();
        m_sheetOpen = false;
        return true;
    }

    bool NavigationService::IsSheetOpen() const noexcept
    {
        return m_sheetOpen;
    }

    Page NavigationService::CurrentPage() const noexcept
    {
        return m_currentPage;
    }

    Page NavigationService::CurrentOverlayPage() const noexcept
    {
        return m_currentOverlayPage;
    }

    bool NavigationService::IsOverlayOpen() const noexcept
    {
        return m_overlayOpen;
    }

    void NavigationService::SetRouteChangedHandler(RouteChangedHandler handler)
    {
        m_routeChangedHandler = std::move(handler);
    }

    void NavigationService::OnNavigated(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args)
    {
        m_currentPage = PageFromType(args.SourcePageType());
        if (m_routeChangedHandler)
        {
            m_routeChangedHandler(m_currentPage);
        }
    }
}
