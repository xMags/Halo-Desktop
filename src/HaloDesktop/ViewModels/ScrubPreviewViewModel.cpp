#include "pch.h"
#include "ViewModels/ScrubPreviewViewModel.h"
#if __has_include("ScrubPreviewViewModel.g.cpp")
#include "ScrubPreviewViewModel.g.cpp"
#endif

#include "Playback/PlaybackPolicy.h"
#include "Playback/ScrubPreviewPolicy.h"
#include "ViewModels/ObservableHelper.h"

#include <cstring>
#include <robuffer.h>
#include <utility>
#include <winrt/Windows.Storage.Streams.h>

namespace
{
    auto const Visible = winrt::Microsoft::UI::Xaml::Visibility::Visible;
    auto const Collapsed = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;

    // Matches SliderHorizontalThumbWidth in PlayerOsd.xaml. The thumb's travel is what
    // the timeline is spread across, so a disagreement here would offset every preview
    // from the position the user is actually pointing at.
    constexpr double SeekThumbWidth = 16.0;
    constexpr double PreviewCardWidth = 200.0;
    constexpr double PreviewCardHeight = 112.0;
    constexpr double HoursThresholdSeconds = 3600.0;
}

namespace winrt::HaloDesktop::implementation
{
    ScrubPreviewViewModel::ScrubPreviewViewModel(
        std::shared_ptr<::HaloDesktop::Playback::IScrubPreviewSource> source)
        : m_source(std::move(source))
    {
    }

    ScrubPreviewViewModel::~ScrubPreviewViewModel()
    {
        Deactivate();
    }

    void ScrubPreviewViewModel::Activate()
    {
        if (!m_source)
        {
            return;
        }

        m_source->SetFrameHandler(
            [weak = get_weak()](::HaloDesktop::Playback::ScrubPreviewFrame frame) {
                if (auto const self = weak.get())
                {
                    self->OnFrame(frame);
                }
            });
    }

    void ScrubPreviewViewModel::Deactivate() noexcept
    {
        if (m_source)
        {
            m_source->ClearFrameHandler();
        }
        m_open = false;
    }

    Microsoft::UI::Xaml::Visibility ScrubPreviewViewModel::PreviewVisibility() const noexcept
    {
        return m_open ? Visible : Collapsed;
    }

    Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap ScrubPreviewViewModel::Image() const
    {
        return m_bitmap;
    }

    Microsoft::UI::Xaml::Visibility ScrubPreviewViewModel::ImageVisibility() const noexcept
    {
        return m_hasImage ? Visible : Collapsed;
    }

    winrt::hstring ScrubPreviewViewModel::TimeText() const
    {
        return m_timeText;
    }

    double ScrubPreviewViewModel::OffsetX() const noexcept
    {
        return m_offsetX;
    }

    double ScrubPreviewViewModel::CardWidth() const noexcept
    {
        return PreviewCardWidth;
    }

    double ScrubPreviewViewModel::CardHeight() const noexcept
    {
        return PreviewCardHeight;
    }

    bool ScrubPreviewViewModel::IsOpen() const noexcept
    {
        return m_open;
    }

    void ScrubPreviewViewModel::Hover(double pointerX, double trackWidth, double durationSeconds)
    {
        auto const time = ::HaloDesktop::Playback::ScrubPreviewTimeFromPointer(
            pointerX,
            trackWidth,
            SeekThumbWidth,
            durationSeconds);
        if (!time.Valid)
        {
            Hide();
            return;
        }

        if (!m_open)
        {
            m_open = true;
            Raise(L"PreviewVisibility");
        }

        auto const offset = ::HaloDesktop::Playback::ClampScrubPreviewOffset(
            pointerX,
            PreviewCardWidth,
            trackWidth);
        if (offset != m_offsetX)
        {
            m_offsetX = offset;
            Raise(L"OffsetX");
        }

        winrt::hstring const text{ ::HaloDesktop::Playback::FormatPlaybackTime(
            time.Seconds,
            durationSeconds >= HoursThresholdSeconds) };
        if (text != m_timeText)
        {
            m_timeText = text;
            Raise(L"TimeText");
        }

        if (m_source)
        {
            m_requestId = m_source->Request(time.Seconds);
        }
    }

    void ScrubPreviewViewModel::Hide()
    {
        if (!m_open)
        {
            return;
        }

        m_open = false;
        // The decoded picture is deliberately kept. Re-entering the seek bar at the same
        // place then shows something immediately instead of an empty card.
        Raise(L"PreviewVisibility");
    }

    void ScrubPreviewViewModel::Reset()
    {
        // A new file invalidates the held picture. Without this an up-next advance would
        // show the previous episode's frame under the new file's timestamps until the
        // first decode of the new source landed.
        m_requestId = 0;
        Hide();
        if (!m_hasImage)
        {
            return;
        }
        m_hasImage = false;
        m_bitmap = nullptr;
        Raise(L"Image");
        Raise(L"ImageVisibility");
    }

    void ScrubPreviewViewModel::OnFrame(::HaloDesktop::Playback::ScrubPreviewFrame const& frame)
    {
        if (frame.RequestId != m_requestId || frame.Width <= 0 || frame.Height <= 0)
        {
            return;
        }

        auto reallocated = false;
        if (!m_bitmap || m_bitmap.PixelWidth() != frame.Width || m_bitmap.PixelHeight() != frame.Height)
        {
            m_bitmap = Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap(frame.Width, frame.Height);
            reallocated = true;
        }

        auto const buffer = m_bitmap.PixelBuffer();
        if (buffer.Capacity() < frame.Bgra.size())
        {
            return;
        }

        auto const access = buffer.as<::Windows::Storage::Streams::IBufferByteAccess>();
        std::uint8_t* pixels{};
        if (FAILED(access->Buffer(&pixels)) || !pixels)
        {
            return;
        }

        std::memcpy(pixels, frame.Bgra.data(), frame.Bgra.size());
        m_bitmap.Invalidate();

        if (reallocated)
        {
            Raise(L"Image");
        }
        if (!m_hasImage)
        {
            m_hasImage = true;
            Raise(L"ImageVisibility");
        }
    }

    winrt::event_token ScrubPreviewViewModel::PropertyChanged(
        Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
    {
        return m_propertyChanged.add(handler);
    }

    void ScrubPreviewViewModel::PropertyChanged(winrt::event_token const& token) noexcept
    {
        m_propertyChanged.remove(token);
    }

    void ScrubPreviewViewModel::Raise(wchar_t const* propertyName)
    {
        ::HaloDesktop::detail::RaisePropertyChanged(m_propertyChanged, *this, propertyName);
    }
}
