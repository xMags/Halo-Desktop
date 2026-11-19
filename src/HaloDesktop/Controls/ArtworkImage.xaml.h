#pragma once

#include "ArtworkImage.g.h"

#include <chrono>
#include <string>
#include <vector>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::HaloDesktop::implementation
{
    // Converts validated URL text to BitmapImage in native code. A failed load
    // stays transparent so the caller's placeholder remains visible.
    //
    // The sources are tried in order: SourceUrl, then FallbackUrl, then
    // metahub's poster when MetaId is an IMDb id, then the placeholder. A
    // missing source is skipped rather than ending the chain. A slot whose
    // DecodeWidth fits the card-sized copy asks TMDB for that copy instead of
    // the original upload.
    //
    // When every source has failed, the whole chain is tried again a few
    // times on a timer (ArtworkRetryDelay), bypassing the image cache, so a
    // brief network fault does not leave a card blank while its page stays
    // open. The timer runs only while the control is loaded.
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
        // The id of the title the artwork belongs to, for metahub's poster.
        [[nodiscard]] winrt::hstring MetaId() const;
        void MetaId(winrt::hstring const& value);
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
        void OnUnloaded(
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
        void TryNextSource();
        // False when the URL could not even be handed to the bitmap.
        [[nodiscard]] bool SetImageSource(winrt::hstring const& value);
        void ShowPlaceholder(bool visible);
        void EnsureFade(Microsoft::UI::Xaml::Controls::Image const& image);
        // Opacity 0 at once, the fade put back for the next picture.
        void HideImage(Microsoft::UI::Xaml::Controls::Image const& image);
        // Arms the next round after the chain ran out, if one is left.
        void ScheduleRetry();
        void StopRetry() noexcept;
        void OnRetryTick();

        winrt::hstring m_sourceUrl;
        winrt::hstring m_fallbackUrl;
        winrt::hstring m_metaId;
        double m_decodeWidth{};
        // The sources already tried since the last refresh, as given (before
        // the card-size rewrite), so a source changing mid-chain is still tried.
        std::vector<std::wstring> m_attempted;
        bool m_exhausted{};
        bool m_opened{};
        // The fade a picture comes in with (Windows' fast 167ms), when
        // animation effects are on. Lifted while the picture is cleared, so a
        // recycled card drops its last picture at once rather than fading it.
        Microsoft::UI::Xaml::ScalarTransition m_fade{ nullptr };
        bool m_fadeChecked{};
        // When the current source was handed to the bitmap, to tell a picture
        // from memory (shown at once) from one fetched (faded in).
        std::chrono::steady_clock::time_point m_requestedAt{};
        // Rounds tried again since the last refresh; a round bypasses the cache.
        int m_retries{};
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_retryTimer{ nullptr };
        Microsoft::UI::Xaml::UIElement m_placeholder{ nullptr };
    };
}

namespace winrt::HaloDesktop::factory_implementation
{
    struct ArtworkImage : ArtworkImageT<ArtworkImage, implementation::ArtworkImage>
    {
    };
}
