#include "pch.h"
#include "Controls/ArtworkImage.xaml.h"
#if __has_include("ArtworkImage.g.cpp")
#include "ArtworkImage.g.cpp"
#endif

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace winrt::HaloDesktop::implementation
{
    ArtworkImage::ArtworkImage() = default;

    winrt::hstring ArtworkImage::SourceUrl() const
    {
        return m_sourceUrl;
    }

    void ArtworkImage::SourceUrl(winrt::hstring const& value)
    {
        if (m_sourceUrl == value)
        {
            return;
        }
        m_sourceUrl = value;
        Refresh();
    }

    winrt::hstring ArtworkImage::FallbackUrl() const
    {
        return m_fallbackUrl;
    }

    void ArtworkImage::FallbackUrl(winrt::hstring const& value)
    {
        m_fallbackUrl = value;
    }

    void ArtworkImage::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        Refresh();
    }

    void ArtworkImage::OnImageOpened(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        sender.as<Microsoft::UI::Xaml::Controls::Image>().Opacity(1);
    }

    void ArtworkImage::OnImageFailed(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::ExceptionRoutedEventArgs const& args)
    {
        auto const image = sender.as<Microsoft::UI::Xaml::Controls::Image>();
        if (!m_fallbackAttempted && !m_fallbackUrl.empty() && m_fallbackUrl != m_sourceUrl)
        {
            m_fallbackAttempted = true;
            SetImageSource(m_fallbackUrl);
            return;
        }
        image.Opacity(0);
    }

    void ArtworkImage::Refresh()
    {
        auto const image = FindName(L"Artwork").try_as<Microsoft::UI::Xaml::Controls::Image>();
        if (!image)
        {
            return;
        }
        m_fallbackAttempted = false;
        image.Opacity(0);
        SetImageSource(m_sourceUrl);
    }

    void ArtworkImage::SetImageSource(winrt::hstring const& value)
    {
        auto const image = FindName(L"Artwork").try_as<Microsoft::UI::Xaml::Controls::Image>();
        if (!image)
        {
            return;
        }
        if (value.empty())
        {
            image.Source(nullptr);
            return;
        }
        try
        {
            image.Source(Microsoft::UI::Xaml::Media::Imaging::BitmapImage{
                winrt::Windows::Foundation::Uri{ value } });
        }
        catch (...)
        {
            image.Source(nullptr);
            image.Opacity(0);
        }
    }
}
