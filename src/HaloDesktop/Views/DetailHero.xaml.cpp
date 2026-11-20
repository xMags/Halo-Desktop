#include "pch.h"
#include "Views/DetailHero.xaml.h"
#if __has_include("DetailHero.g.cpp")
#include "DetailHero.g.cpp"
#endif

#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace
{
    using winrt::Microsoft::UI::Xaml::Hosting::ElementCompositionPreview;

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
}

namespace winrt::HaloDesktop::implementation
{
    void DetailHero::ViewModel(winrt::HaloDesktop::DetailViewModel const& value)
    {
        m_viewModel = value;
        m_viewModelChanged.revoke();
        if (!m_viewModel) return;
        m_viewModelChanged = m_viewModel.PropertyChanged(winrt::auto_revoke, { get_weak(), &DetailHero::OnViewModelChanged });
        LoadLogo();
    }

    void DetailHero::ApplyLayout(double gutter, bool showAside, double heroHeight)
    {
        HeroInner().Padding({ gutter, 72.0, gutter, 40.0 });
        WaysToWatch().Visibility(showAside ? Microsoft::UI::Xaml::Visibility::Visible : Microsoft::UI::Xaml::Visibility::Collapsed);
        HeroRoot().MinHeight(heroHeight);
    }

    void DetailHero::PlayEntrance()
    {
        if (!AnimationsEnabled()) return;
        try
        {
            if (auto const storyboard = Resources().Lookup(winrt::box_value(winrt::hstring{ L"RiseStoryboard" })).try_as<Microsoft::UI::Xaml::Media::Animation::Storyboard>())
            {
                storyboard.Stop();
                storyboard.Begin();
            }
        }
        catch (winrt::hresult_error const&)
        {
            // The blocks simply stand where they are.
        }
    }

    Microsoft::UI::Xaml::FrameworkElement DetailHero::PlayButtonElement()
    {
        return PlayButton();
    }

    void DetailHero::OnPlayClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_viewModel) m_viewModel.Play();
    }

    void DetailHero::OnAddAddonClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_viewModel) m_viewModel.OpenAddonSettings();
    }

    void DetailHero::OnLibraryClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_viewModel) m_viewModel.ToggleLibrary();
    }

    void DetailHero::OnTrailerClick(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        if (m_viewModel) m_viewModel.OpenTrailer();
    }

    void DetailHero::OnLogoFailed(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::ExceptionRoutedEventArgs const&)
    {
        if (m_viewModel) m_viewModel.LogoFailed();
        UpdateLogoShadow();
    }

    void DetailHero::OnLogoOpened(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        UpdateLogoShadow();
    }

    void DetailHero::OnLogoSizeChanged(winrt::Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::SizeChangedEventArgs const&)
    {
        UpdateLogoShadow();
    }

    void DetailHero::OnViewModelChanged(
        winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::Data::PropertyChangedEventArgs const& args)
    {
        auto const name = args.PropertyName();
        if (name == L"Logo")
        {
            LoadLogo();
        }
        else if (name == L"LogoVisibility")
        {
            UpdateLogoShadow();
        }
    }

    void DetailHero::LoadLogo()
    {
        auto const logo = m_viewModel ? m_viewModel.Logo() : winrt::hstring{};
        if (logo == m_logo) return;
        m_logo = logo;
        auto const image = LogoImage();
        if (logo.empty())
        {
            image.Source(nullptr);
            UpdateLogoShadow();
            return;
        }
        try
        {
            Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
            bitmap.UriSource(winrt::Windows::Foundation::Uri{ logo });
            image.Source(bitmap);
        }
        catch (winrt::hresult_error const&)
        {
            // A URL the bitmap refuses outright falls back to the name, as a
            // logo that fails to load does.
            image.Source(nullptr);
            if (m_viewModel) m_viewModel.LogoFailed();
        }
    }

    void DetailHero::UpdateLogoShadow()
    {
        if (m_shadowUnavailable) return;
        auto const host = LogoShadowHost();
        auto const image = LogoImage();
        try
        {
            auto const shown = image.Visibility() == Microsoft::UI::Xaml::Visibility::Visible && image.Source() != nullptr
                && image.ActualWidth() > 0.0 && image.ActualHeight() > 0.0;
            if (!m_logoShadow)
            {
                if (!shown) return;
                auto const compositor = ElementCompositionPreview::GetElementVisual(host).Compositor();
                m_logoDropShadow = compositor.CreateDropShadow();
                m_logoDropShadow.BlurRadius(24.0F);
                m_logoDropShadow.Offset({ 0.0F, 4.0F, 0.0F });
                m_logoDropShadow.Color({ 0x80, 0x00, 0x00, 0x00 });
                m_logoShadow = compositor.CreateSpriteVisual();
                m_logoShadow.Shadow(m_logoDropShadow);
                ElementCompositionPreview::SetElementChildVisual(host, m_logoShadow);
            }
            m_logoShadow.IsVisible(shown);
            if (!shown) return;
            auto const width = image.ActualWidth();
            auto const height = image.ActualHeight();
            host.Width(width);
            host.Height(height);
            m_logoShadow.Size({ static_cast<float>(width), static_cast<float>(height) });
            m_logoDropShadow.Mask(image.GetAlphaMask());
        }
        catch (winrt::hresult_error const&)
        {
            m_shadowUnavailable = true;
            try
            {
                ElementCompositionPreview::SetElementChildVisual(host, nullptr);
            }
            catch (winrt::hresult_error const&)
            {
            }
            m_logoShadow = nullptr;
            m_logoDropShadow = nullptr;
        }
    }
}
