#pragma once

#include <functional>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Windows.Foundation.h>

namespace HaloDesktop::Services
{
    enum class Page
    {
        Home,
        Search,
        Library,
        Detail,
        Sources,
        Downloads,
        Settings,
        Connect,
        Login,
        Player,
    };

    class NavigationService final
    {
    public:
        using RouteChangedHandler = std::function<void(Page)>;

        NavigationService() = default;
        ~NavigationService();

        NavigationService(NavigationService const&) = delete;
        NavigationService& operator=(NavigationService const&) = delete;
        NavigationService(NavigationService&&) = delete;
        NavigationService& operator=(NavigationService&&) = delete;

        void AttachShellFrame(winrt::Microsoft::UI::Xaml::Controls::Frame const& frame);
        void AttachOverlayFrame(winrt::Microsoft::UI::Xaml::Controls::Frame const& frame);
        void Detach() noexcept;

        bool GoTo(
            Page page,
            winrt::Windows::Foundation::IInspectable const& parameter = nullptr);
        bool GoBack();
        bool ShowOverlay(
            Page page,
            winrt::Windows::Foundation::IInspectable const& parameter = nullptr);
        bool CloseOverlay();

        [[nodiscard]] bool CanGoBack() const noexcept;
        [[nodiscard]] Page CurrentPage() const noexcept;
        [[nodiscard]] Page CurrentOverlayPage() const noexcept;
        [[nodiscard]] bool IsOverlayOpen() const noexcept;
        void SetRouteChangedHandler(RouteChangedHandler handler);

    private:
        void OnNavigated(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args);

        winrt::Microsoft::UI::Xaml::Controls::Frame m_shellFrame{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Frame m_overlayFrame{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Frame::Navigated_revoker m_shellNavigatedRevoker{};
        RouteChangedHandler m_routeChangedHandler;
        Page m_currentPage{ Page::Home };
        Page m_currentOverlayPage{ Page::Connect };
        bool m_overlayOpen{};
    };
}
