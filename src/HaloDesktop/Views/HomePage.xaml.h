#pragma once

#include "HomePage.g.h"

#include "Controls/SkeletonPulse.h"
#include "Views/HomeHeroPresenter.h"
#include "Views/TitleMenu.h"

#include <memory>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

namespace winrt::HaloDesktop::implementation
{
    struct HomePage : HomePageT<HomePage>
    {
        HomePage();
        void OnLoaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnUnloaded(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        [[nodiscard]] winrt::HaloDesktop::HomeViewModel ViewModel() const;
        void OnSearchSubmitted(
            Microsoft::UI::Xaml::Controls::AutoSuggestBox const&,
            Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const&);
        void OnRetryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnOpenSettingsClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAllFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnMoviesFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSeriesFilterClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFeaturedActionClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFeaturedDetailsClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFeaturedLibraryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnFeaturedPointerEntered(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnFeaturedPointerExited(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnFeaturedKeyDown(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void OnHeroPipClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnContinueItemClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        // The strips scroll by their own arrows only. A tilt wheel or a shift
        // wheel over one would otherwise pan it, which no other list here does.
        void OnStripPointerWheelChanged(
            winrt::Windows::Foundation::IInspectable const&,
            Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&);
        void OnContinueSeeAllClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnContinueScrollLeft(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnContinueScrollRight(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnContinueViewChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const&);
        void OnContinueScrollerSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);
        void OnShelfItemClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnShelfSeeAllClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        // A shelf element realised or recycled: its skeletons follow the page's
        // one pulse.
        void OnShelfElementPrepared(
            Microsoft::UI::Xaml::Controls::ItemsRepeater const&,
            Microsoft::UI::Xaml::Controls::ItemsRepeaterElementPreparedEventArgs const&);
        // One handler for the page: the banner, the continue strip and every shelf
        // poster all answer a right-click from here.
        void OnContextRequested(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::ContextRequestedEventArgs const&);

    private:
        void ScrollContinueBy(int direction);
        void UpdateContinueEdges();
        void EnsureHeroPresenter();
        void SyncHero();
        void EnsureSkeletonPulse();
        // Runs the pulse while Home is on screen and shows a skeleton.
        void UpdateSkeletonPulse();

        winrt::HaloDesktop::HomeViewModel m_viewModel{ nullptr };
        ::HaloDesktop::Views::TitleMenu m_titleMenu;
        std::unique_ptr<::HaloDesktop::Views::HomeHeroPresenter> m_hero;
        // The one clock every skeleton on the page pulses with.
        std::unique_ptr<::HaloDesktop::Controls::SkeletonPulse> m_skeletonPulse;
        bool m_listsBound{};
        bool m_onScreen{};
        // Last, so both are revoked before the presenter and the view model go.
        Microsoft::UI::Xaml::Data::INotifyPropertyChanged::PropertyChanged_revoker m_viewModelChanged;
        Microsoft::UI::Xaml::FrameworkElement::ActualThemeChanged_revoker m_themeChanged;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct HomePage : HomePageT<HomePage, implementation::HomePage>
    {
    };
}
