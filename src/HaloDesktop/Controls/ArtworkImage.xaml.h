#pragma once

#include "ArtworkImage.g.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    // Converts validated URL text to BitmapImage in native code. A failed load
    // stays transparent so the caller's placeholder remains visible.
    struct ArtworkImage : ArtworkImageT<ArtworkImage>
    {
        ArtworkImage();

        [[nodiscard]] winrt::hstring SourceUrl() const;
        void SourceUrl(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring FallbackUrl() const;
        void FallbackUrl(winrt::hstring const& value);

        void OnLoaded(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnImageOpened(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnImageFailed(
            winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::ExceptionRoutedEventArgs const& args);

    private:
        void Refresh();
        void SetImageSource(winrt::hstring const& value);

        winrt::hstring m_sourceUrl;
        winrt::hstring m_fallbackUrl;
        bool m_fallbackAttempted{};
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct ArtworkImage : ArtworkImageT<ArtworkImage, implementation::ArtworkImage>
    {
    };
}
