#pragma once

#include "DetailHero.g.h"

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Windows.Web.Http.h>

namespace winrt::HaloDesktop::implementation
{
    struct DetailHero : DetailHeroT<DetailHero>
    {
        DetailHero() = default;

        [[nodiscard]] winrt::HaloDesktop::DetailViewModel ViewModel() const { return m_viewModel; }
        // Set once by the page before the control loads.
        void ViewModel(winrt::HaloDesktop::DetailViewModel const& value);

        // The page's responsive metrics. Not projected.
        void ApplyLayout(double gutter, bool showAside, double heroHeight);
        // The blocks rise into place once per title, unless the system has
        // animations turned off. Not projected.
        void PlayEntrance();
        // The main button, which the compact bar watches scroll out of view.
        [[nodiscard]] Microsoft::UI::Xaml::FrameworkElement PlayButtonElement();

        void OnPlayClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnAddAddonClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnLibraryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnTrailerClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnLogoFailed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::ExceptionRoutedEventArgs const&);
        void OnLogoOpened(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnLogoSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&);

    private:
        void OnViewModelChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args);
        void LoadLogo();
        // The light theme's logo: fetched and judged as Home's banner judges
        // its own (ChooseHeroLogoTreatment), then shown as drawn, inked in the
        // page's ink, or given up for the name. Stale once m_logo moves on.
        winrt::fire_and_forget LoadJudgedLogoAsync(winrt::hstring url);
        [[nodiscard]] bool IsLight() const;
        // The logo's `drop-shadow(0 4px 24px rgba(0,0,0,.5))`, cast by its alpha.
        // Decoration only: any refusal hides it for good.
        void UpdateLogoShadow();

        winrt::HaloDesktop::DetailViewModel m_viewModel{ nullptr };
        winrt::hstring m_logo;
        winrt::Windows::Web::Http::HttpClient m_logoClient{ nullptr };
        Microsoft::UI::Xaml::FrameworkElement::ActualThemeChanged_revoker m_themeChanged;
        winrt::Microsoft::UI::Composition::SpriteVisual m_logoShadow{ nullptr };
        winrt::Microsoft::UI::Composition::DropShadow m_logoDropShadow{ nullptr };
        bool m_shadowUnavailable{};
        winrt::Microsoft::UI::Xaml::Data::INotifyPropertyChanged::PropertyChanged_revoker m_viewModelChanged;
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct DetailHero : DetailHeroT<DetailHero, implementation::DetailHero>
    {
    };
}
