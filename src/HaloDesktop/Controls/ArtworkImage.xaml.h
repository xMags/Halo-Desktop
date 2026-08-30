#pragma once

#include "ArtworkImage.g.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    // Converts validated URL text to BitmapImage in native code. A failed load
    // stays transparent so the caller's placeholder remains visible.
    //
    // Hand it that placeholder and it will also take it away once the artwork is
    // up. The placeholder is opaque, so one left behind a loaded image means the
    // card draws its art twice for as long as it is on screen.
    struct ArtworkImage : ArtworkImageT<ArtworkImage>
    {
        ArtworkImage();

        [[nodiscard]] winrt::hstring SourceUrl() const;
        void SourceUrl(winrt::hstring const& value);
        [[nodiscard]] winrt::hstring FallbackUrl() const;
        void FallbackUrl(winrt::hstring const& value);
        // Logical pixels. Zero leaves the source at full resolution, which is
        // right for a full-bleed backdrop and wasteful for anything smaller.
        [[nodiscard]] double DecodeWidth() const noexcept;
        void DecodeWidth(double value);
        // Optional. Shown while there is no artwork up, collapsed once there is.
        [[nodiscard]] Microsoft::UI::Xaml::UIElement Placeholder() const;
        void Placeholder(Microsoft::UI::Xaml::UIElement const& value);

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
        void ShowPlaceholder(bool visible);

        winrt::hstring m_sourceUrl;
        winrt::hstring m_fallbackUrl;
        double m_decodeWidth{};
        bool m_fallbackAttempted{};
        bool m_opened{};
        Microsoft::UI::Xaml::UIElement m_placeholder{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct ArtworkImage : ArtworkImageT<ArtworkImage, implementation::ArtworkImage>
    {
    };
}
