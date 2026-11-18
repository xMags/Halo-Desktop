#include "pch.h"
#include "Views/SignInPosterFan.xaml.h"
#if __has_include("SignInPosterFan.g.cpp")
#include "SignInPosterFan.g.cpp"
#endif

#include "Views/SignInMotion.h"

#include <array>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace
{
    // The reference's posters, left to right, from the public image host
    // Cinemeta's own metadata points at (the same titles the phone shows).
    constexpr std::array<wchar_t const*, 5> PosterUrls{
        L"https://images.metahub.space/poster/medium/tt11280740/img",
        L"https://images.metahub.space/poster/medium/tt3581920/img",
        L"https://images.metahub.space/poster/medium/tt15398776/img",
        L"https://images.metahub.space/poster/medium/tt4574334/img",
        L"https://images.metahub.space/poster/medium/tt1375666/img",
    };

    constexpr wchar_t const* EntranceStoryboard = L"EntranceStoryboard";
}

namespace winrt::HaloDesktop::implementation
{
    void SignInPosterFan::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        LoadPosters();
        if (::HaloDesktop::Views::SignInAnimationsEnabled())
        {
            ::HaloDesktop::Views::BeginSignInStoryboard(Resources(), EntranceStoryboard);
        }
    }

    void SignInPosterFan::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const&,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        ::HaloDesktop::Views::StopSignInStoryboard(Resources(), EntranceStoryboard);
    }

    void SignInPosterFan::OnPosterFailed(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::ExceptionRoutedEventArgs const&)
    {
        // The poster keeps its dark card, as the reference hides the image only.
        if (auto const brush = sender.try_as<Microsoft::UI::Xaml::Media::ImageBrush>())
        {
            try
            {
                brush.Opacity(0.0);
            }
            catch (winrt::hresult_error const&)
            {
            }
        }
    }

    void SignInPosterFan::LoadPosters()
    {
        if (m_postersLoaded)
        {
            return;
        }
        m_postersLoaded = true;
        std::array<Microsoft::UI::Xaml::Media::ImageBrush, 5> const brushes{
            Poster1Image(), Poster2Image(), Poster3Image(), Poster4Image(), Poster5Image(),
        };
        for (std::size_t index = 0; index < brushes.size(); ++index)
        {
            try
            {
                brushes[index].ImageSource(Microsoft::UI::Xaml::Media::Imaging::BitmapImage{
                    winrt::Windows::Foundation::Uri{ PosterUrls[index] } });
            }
            catch (winrt::hresult_error const&)
            {
                // The dark card stays empty.
            }
        }
    }
}
