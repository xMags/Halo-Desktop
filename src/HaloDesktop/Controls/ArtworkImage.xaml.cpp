#include "pch.h"
#include "Controls/ArtworkImage.xaml.h"
#if __has_include("ArtworkImage.g.cpp")
#include "ArtworkImage.g.cpp"
#endif

#include "Controls/ArtworkUrlPolicy.h"

#include <algorithm>
#include <chrono>
#include <string_view>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace
{
    // Under two frames from asking to open: the picture was already decoded
    // or cached, not fetched.
    constexpr std::chrono::milliseconds InstantOpen{ 40 };
}

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
        if (m_fallbackUrl == value) return;
        m_fallbackUrl = value;
        // A source that arrives after the others failed, or were missing, is
        // still worth a try; while one is loading it simply joins the chain.
        if (m_exhausted) TryNextSource();
    }

    winrt::hstring ArtworkImage::MetaId() const
    {
        return m_metaId;
    }

    void ArtworkImage::MetaId(winrt::hstring const& value)
    {
        if (m_metaId == value) return;
        m_metaId = value;
        // Bindings set the id and the urls in either order, and a recycled image
        // gets them all anew. Only an image on its own SourceUrl can keep it (as
        // PosterCard does); one on a fallback, which may be the previous title's
        // metahub poster, or on nothing at all, starts over for the new id.
        // An exhausted chain only needs the new id's own candidate tried.
        if (m_exhausted)
        {
            TryNextSource();
            return;
        }
        auto const onOwnSource = !m_sourceUrl.empty()
            && m_attempted.size() == 1 && m_attempted.front() == std::wstring_view{ m_sourceUrl };
        if (!onOwnSource) Refresh();
    }

    double ArtworkImage::DecodeWidth() const noexcept { return m_decodeWidth; }
    void ArtworkImage::DecodeWidth(double value) { m_decodeWidth = value; }

    Microsoft::UI::Xaml::UIElement ArtworkImage::Placeholder() const { return m_placeholder; }
    void ArtworkImage::Placeholder(Microsoft::UI::Xaml::UIElement const& value)
    {
        m_placeholder = value;
        // A recycled control can be handed its placeholder after the artwork it
        // already holds has opened, so adopt the current state rather than
        // assuming this is a fresh load.
        ShowPlaceholder(!m_opened);
    }

    void ArtworkImage::EnsureFade(Microsoft::UI::Xaml::Controls::Image const& image)
    {
        if (m_fadeChecked || !image) return;
        m_fadeChecked = true;
        try
        {
            if (!winrt::Windows::UI::ViewManagement::UISettings{}.AnimationsEnabled()) return;
            m_fade = Microsoft::UI::Xaml::ScalarTransition{};
            m_fade.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::milliseconds{ 167 }));
            image.OpacityTransition(m_fade);
        }
        catch (winrt::hresult_error const&)
        {
            m_fade = nullptr;
        }
    }

    void ArtworkImage::HideImage(Microsoft::UI::Xaml::Controls::Image const& image)
    {
        if (!image) return;
        if (m_fade) image.OpacityTransition(nullptr);
        image.Opacity(0);
        if (m_fade) image.OpacityTransition(m_fade);
    }

    void ArtworkImage::ShowPlaceholder(bool visible)
    {
        if (!m_placeholder) return;
        m_placeholder.Visibility(visible
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed);
    }

    // Only builds the bitmap when there is not already one for this url. Loaded
    // fires again every time a recycled item comes back into view, and refreshing
    // unconditionally made that a fresh decode each pass.
    void ArtworkImage::OnLoaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const image = FindName(L"Artwork").try_as<Microsoft::UI::Xaml::Controls::Image>();
        EnsureFade(image);
        if (image && image.Source() != nullptr) return;
        Refresh();
    }

    // Nothing waits on a control off screen; coming back loads afresh (OnLoaded).
    void ArtworkImage::OnUnloaded(
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        StopRetry();
    }

    void ArtworkImage::OnImageOpened(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        auto const image = sender.as<Microsoft::UI::Xaml::Controls::Image>();
        // A picture that opens at once came from memory: a card scrolled back
        // into view, or one shown before. It is simply there, as it is in a
        // browser; only a picture that had to be fetched fades in.
        auto const instant = std::chrono::steady_clock::now() - m_requestedAt < InstantOpen;
        if (m_fade && instant) image.OpacityTransition(nullptr);
        image.Opacity(1);
        if (m_fade && instant) image.OpacityTransition(m_fade);
        m_opened = true;
        StopRetry();
        ShowPlaceholder(false);
    }

    void ArtworkImage::OnImageFailed(
        winrt::Windows::Foundation::IInspectable const& sender,
        [[maybe_unused]] Microsoft::UI::Xaml::ExceptionRoutedEventArgs const& args)
    {
        HideImage(sender.as<Microsoft::UI::Xaml::Controls::Image>());
        m_opened = false;
        ShowPlaceholder(true);
        TryNextSource();
    }

    void ArtworkImage::Refresh()
    {
        auto const image = FindName(L"Artwork").try_as<Microsoft::UI::Xaml::Controls::Image>();
        if (!image)
        {
            return;
        }
        m_attempted.clear();
        m_exhausted = false;
        m_retries = 0;
        StopRetry();
        EnsureFade(image);
        HideImage(image);
        // Back to the placeholder until the new artwork actually opens, or a
        // recycled card would keep showing the previous item's picture.
        m_opened = false;
        ShowPlaceholder(true);
        TryNextSource();
    }

    void ArtworkImage::TryNextSource()
    {
        auto const image = FindName(L"Artwork").try_as<Microsoft::UI::Xaml::Controls::Image>();
        if (!image)
        {
            return;
        }
        // At most three sources, so a URL the bitmap refuses outright moves on
        // in a bounded loop rather than waiting for a failure event.
        for (auto const& candidate : ::HaloDesktop::Controls::ArtworkCandidates(m_sourceUrl, m_fallbackUrl, m_metaId))
        {
            if (std::find(m_attempted.begin(), m_attempted.end(), candidate) != m_attempted.end())
            {
                continue;
            }
            m_attempted.push_back(candidate);
            auto const url = ::HaloDesktop::Controls::UsesCardArtwork(m_decodeWidth)
                ? ::HaloDesktop::Controls::CardArtworkUrl(candidate)
                : candidate;
            if (SetImageSource(winrt::hstring{ url }))
            {
                m_exhausted = false;
                return;
            }
        }
        image.Source(nullptr);
        HideImage(image);
        m_exhausted = true;
        // Something was there to show and none of it loaded: worth another go.
        if (!m_attempted.empty()) ScheduleRetry();
    }

    void ArtworkImage::ScheduleRetry()
    {
        auto const delay = ::HaloDesktop::Controls::ArtworkRetryDelay(m_retries);
        if (!delay || !IsLoaded()) return;
        try
        {
            if (!m_retryTimer)
            {
                auto const queue = DispatcherQueue();
                if (!queue) return;
                m_retryTimer = queue.CreateTimer();
                m_retryTimer.IsRepeating(false);
                m_retryTimer.Tick([weak = get_weak()](auto const&, auto const&)
                {
                    if (auto const self = weak.get()) self->OnRetryTick();
                });
            }
            m_retryTimer.Interval(*delay);
            m_retryTimer.Start();
        }
        catch (winrt::hresult_error const&)
        {
            // Without a timer the card keeps its placeholder until it is shown again.
        }
    }

    void ArtworkImage::StopRetry() noexcept
    {
        if (!m_retryTimer) return;
        try
        {
            m_retryTimer.Stop();
        }
        catch (winrt::hresult_error const&)
        {
        }
    }

    // A source that arrived or opened meanwhile has already done the work.
    void ArtworkImage::OnRetryTick()
    {
        if (m_opened || !m_exhausted || !IsLoaded()) return;
        ++m_retries;
        m_attempted.clear();
        m_exhausted = false;
        TryNextSource();
    }

    bool ArtworkImage::SetImageSource(winrt::hstring const& value)
    {
        auto const image = FindName(L"Artwork").try_as<Microsoft::UI::Xaml::Controls::Image>();
        if (!image || value.empty())
        {
            return false;
        }
        try
        {
            Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
            // Set before the uri: assigning the source starts the decode.
            if (m_decodeWidth > 0.0) bitmap.DecodePixelWidth(static_cast<std::int32_t>(m_decodeWidth));
            // A retry asks the network again rather than a failure it remembers.
            if (m_retries > 0) bitmap.CreateOptions(Microsoft::UI::Xaml::Media::Imaging::BitmapCreateOptions::IgnoreImageCache);
            m_requestedAt = std::chrono::steady_clock::now();
            bitmap.UriSource(winrt::Windows::Foundation::Uri{ value });
            image.Source(bitmap);
            return true;
        }
        catch (...)
        {
            image.Source(nullptr);
            HideImage(image);
            return false;
        }
    }
}
