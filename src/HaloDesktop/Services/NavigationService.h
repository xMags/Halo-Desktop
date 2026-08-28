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
        Catalog,
        Detail,
        Sources,
        Downloads,
        Settings,
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
        // The sheet layer draws over the shell without replacing it, so the page
        // the sheet was opened from keeps rendering, and its scroll position, behind.
        void AttachSheetFrame(winrt::Microsoft::UI::Xaml::Controls::Frame const& frame);
        void Detach() noexcept;

        bool GoTo(
            Page page,
            winrt::Windows::Foundation::IInspectable const& parameter = nullptr);
        bool GoBack();
        bool ShowOverlay(
            Page page,
            winrt::Windows::Foundation::IInspectable const& parameter = nullptr);
        bool CloseOverlay();
        bool ShowSheet(
            Page page,
            winrt::Windows::Foundation::IInspectable const& parameter = nullptr);
        bool CloseSheet();

        [[nodiscard]] bool CanGoBack() const noexcept;
        [[nodiscard]] Page CurrentPage() const noexcept;
        [[nodiscard]] Page CurrentOverlayPage() const noexcept;
        [[nodiscard]] bool IsOverlayOpen() const noexcept;
        [[nodiscard]] bool IsSheetOpen() const noexcept;
        void SetRouteChangedHandler(RouteChangedHandler handler);

    private:
        void OnNavigated(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args);

        winrt::Microsoft::UI::Xaml::Controls::Frame m_shellFrame{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Frame m_overlayFrame{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Frame m_sheetFrame{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::Frame::Navigated_revoker m_shellNavigatedRevoker{};
        RouteChangedHandler m_routeChangedHandler;
        Page m_currentPage{ Page::Home };
        Page m_currentOverlayPage{ Page::Login };
        bool m_overlayOpen{};
        bool m_sheetOpen{};
    };
}
