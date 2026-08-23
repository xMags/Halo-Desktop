#include "pch.h"
#include "Services/NavigationService.h"

#include "Views/DetailPage.xaml.h"
#include "Views/DownloadsPage.xaml.h"
#include "Views/HomePage.xaml.h"
#include "Views/LibraryPage.xaml.h"
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
        }

        throw std::invalid_argument("Unknown shell page");
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

        Detach();
        m_shellFrame = frame;
        m_navigatedRevoker = m_shellFrame.Navigated(
            winrt::auto_revoke,
            [this](
                winrt::Windows::Foundation::IInspectable const& sender,
                winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args)
            {
                OnNavigated(sender, args);
            });
    }

    void NavigationService::Detach() noexcept
    {
        m_navigatedRevoker.revoke();
        m_shellFrame = nullptr;
        m_routeChangedHandler = {};
    }

    bool NavigationService::GoTo(
        Page page,
        winrt::Windows::Foundation::IInspectable const& parameter)
    {
        if (!m_shellFrame)
        {
            return false;
        }

        auto const targetType = PageType(page);
        if (m_shellFrame.Content() && m_shellFrame.CurrentSourcePageType().Name == targetType.Name)
        {
            return false;
        }

        auto const transition = winrt::Microsoft::UI::Xaml::Media::Animation::EntranceNavigationTransitionInfo{};
        return m_shellFrame.Navigate(targetType, parameter, transition);
    }

    bool NavigationService::GoBack()
    {
        if (!CanGoBack())
        {
            return false;
        }

        m_shellFrame.GoBack();
        return true;
    }

    bool NavigationService::CanGoBack() const noexcept
    {
        return m_shellFrame && m_shellFrame.CanGoBack();
    }

    Page NavigationService::CurrentPage() const noexcept
    {
        return m_currentPage;
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
