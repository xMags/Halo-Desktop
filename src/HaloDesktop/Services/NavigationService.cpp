#include "pch.h"
#include "Services/NavigationService.h"

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

    void NavigationService::Detach() noexcept
    {
        m_shellNavigatedRevoker.revoke();
        m_shellFrame = nullptr;
        m_overlayFrame = nullptr;
        m_overlayOpen = false;
        m_routeChangedHandler = {};
    }

    bool NavigationService::GoTo(
        Page page,
        winrt::Windows::Foundation::IInspectable const& parameter)
    {
        if (!m_shellFrame || IsOverlayPage(page))
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
