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

    double ArtworkImage::DecodeWidth() const noexcept { return m_decodeWidth; }
    void ArtworkImage::DecodeWidth(double value) { m_decodeWidth = value; }

    // Only builds the bitmap when there is not already one for this url. Loaded
    // fires again every time a recycled item comes back into view, and refreshing
    // unconditionally made that a fresh decode each pass.
    void ArtworkImage::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const image = FindName(L"Artwork").try_as<Microsoft::UI::Xaml::Controls::Image>();
        if (image && image.Source() != nullptr) return;
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
            Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
            // Set before the uri: assigning the source starts the decode.
            if (m_decodeWidth > 0.0) bitmap.DecodePixelWidth(static_cast<std::int32_t>(m_decodeWidth));
            bitmap.UriSource(winrt::Windows::Foundation::Uri{ value });
            image.Source(bitmap);
        }
        catch (...)
        {
            image.Source(nullptr);
            image.Opacity(0);
        }
    }
}
