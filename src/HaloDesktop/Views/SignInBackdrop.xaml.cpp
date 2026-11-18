#include "pch.h"
#include "Views/SignInBackdrop.xaml.h"
#if __has_include("SignInBackdrop.g.cpp")
#include "SignInBackdrop.g.cpp"
#endif

#include "Views/SignInMotion.h"

#include <array>
#include <cstdint>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace
{
    // The reference's backdrops, from the public image host Cinemeta's own
    // metadata points at. Signed out there is no addon list to ask, so the
    // titles are fixed.
    constexpr std::array<wchar_t const*, 3> BackdropUrls{
        L"https://images.metahub.space/background/medium/tt15239678/img",
        L"https://images.metahub.space/background/medium/tt2543164/img",
        L"https://images.metahub.space/background/medium/tt1877830/img",
    };

    // Decoded this narrow and stretched over the screen: the upscale smooths
    // each backdrop into the soft colour the reference's 30px blur leaves.
    constexpr std::int32_t BackdropDecodeWidth = 72;

    // `.si-glow-c`: left 48%, top 38%.
    constexpr double GlowLeftShare = 0.48;
    constexpr double GlowTopShare = 0.38;

    constexpr wchar_t const* CrossfadeStoryboard = L"CrossfadeStoryboard";
}

namespace winrt::HaloDesktop::implementation
{
    void SignInBackdrop::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        LoadArt();
        if (::HaloDesktop::Views::SignInAnimationsEnabled())
        {
            ::HaloDesktop::Views::BeginSignInStoryboard(Resources(), CrossfadeStoryboard);
        }
    }

    void SignInBackdrop::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ::HaloDesktop::Views::StopSignInStoryboard(Resources(), CrossfadeStoryboard);
    }

    void SignInBackdrop::OnSizeChanged(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        try
        {
            auto const size = args.NewSize();
            Microsoft::UI::Xaml::Controls::Canvas::SetLeft(GlowC(), size.Width * GlowLeftShare);
            Microsoft::UI::Xaml::Controls::Canvas::SetTop(GlowC(), size.Height * GlowTopShare);
        }
        catch (winrt::hresult_error const&)
        {
            // The glow stays where it was.
        }
    }

    void SignInBackdrop::LoadArt()
    {
        if (m_artLoaded)
        {
            return;
        }
        m_artLoaded = true;
        std::array<Microsoft::UI::Xaml::Controls::Image, 3> const images{ Backdrop1(), Backdrop2(), Backdrop3() };
        for (std::size_t index = 0; index < images.size(); ++index)
        {
            try
            {
                Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
                // Set before the URI, or the decode has already been scheduled at full size.
                bitmap.DecodePixelWidth(BackdropDecodeWidth);
                bitmap.UriSource(winrt::Windows::Foundation::Uri{ BackdropUrls[index] });
                images[index].Source(bitmap);
            }
            catch (winrt::hresult_error const&)
            {
                // A backdrop that cannot load leaves the dark ground.
            }
        }
    }
}
