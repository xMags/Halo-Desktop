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

        if (auto const dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread())
        {
            m_skeletonTimer = dispatcher.CreateTimer();
            m_skeletonTimer.Interval(::HaloDesktop::Playback::ScrubPreviewSkeletonDelay);
            m_skeletonTimer.IsRepeating(false);
            m_skeletonTickRevoker = m_skeletonTimer.Tick(
                winrt::auto_revoke,
                [weak = get_weak()](
                    [[maybe_unused]] Microsoft::UI::Dispatching::DispatcherQueueTimer const& timer,
                    [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args) {
                    if (auto const self = weak.get())
                    {
                        self->SetSkeleton(::HaloDesktop::Playback::IsScrubPreviewLoading(
                            self->m_requestId,
                            self->m_answeredId));
                    }
                });
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
        try
        {
            if (m_skeletonTimer)
            {
                m_skeletonTimer.Stop();
            }
        }
        catch (...)
        {
        }
        m_skeletonTickRevoker.revoke();
        m_skeletonTimer = nullptr;
        m_skeleton = false;
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
        // Once a wait is long enough to show the skeleton, the held picture is the old
        // place's frame, which is no answer for the new place.
        return m_hasImage && !m_skeleton ? Visible : Collapsed;
    }

    Microsoft::UI::Xaml::Visibility ScrubPreviewViewModel::SkeletonVisibility() const noexcept
    {
        return m_skeleton ? Visible : Collapsed;
    }

    bool ScrubPreviewViewModel::SkeletonActive() const noexcept
    {
        return m_open && m_skeleton;
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

        SetOpen(true);

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
            UpdateLoading();
        }
    }

    void ScrubPreviewViewModel::Hide()
    {
        // The decoded picture is deliberately kept. Re-entering the seek bar at the same
        // place then shows something immediately instead of an empty card.
        SetOpen(false);
    }

    void ScrubPreviewViewModel::Reset()
    {
        // A new file invalidates the held picture. Without this an up-next advance would
        // show the previous episode's frame under the new file's timestamps until the
        // first decode of the new source landed.
        m_requestId = 0;
        m_answeredId = 0;
        UpdateLoading();
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
        // The source answers in request order, so the last answer is the newest one. An
        // answer to an older request is still recorded: UpdateLoading compares it with
        // the current id, so it ends no wait but the one it belongs to.
        m_answeredId = frame.RequestId;
        if (frame.RequestId == m_requestId)
        {
            ShowFrame(frame);
        }
        UpdateLoading();
    }

    // Paints the answer to the card's own request. An answer with no picture, or one that
    // cannot be copied, empties the box instead of leaving the previous place's frame
    // under this place's time.
    void ScrubPreviewViewModel::ShowFrame(::HaloDesktop::Playback::ScrubPreviewFrame const& frame)
    {
        if (frame.Width <= 0 || frame.Height <= 0 || frame.Bgra.empty())
        {
            ClearImage();
            return;
        }

        if (!m_bitmap || m_bitmap.PixelWidth() != frame.Width || m_bitmap.PixelHeight() != frame.Height)
        {
            m_bitmap = Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap(frame.Width, frame.Height);
            // Raised before the copy, so a copy that fails cannot leave the Image bound
            // to the bitmap this one replaced.
            Raise(L"Image");
        }

        auto const buffer = m_bitmap.PixelBuffer();
        if (buffer.Capacity() < frame.Bgra.size())
        {
            ClearImage();
            return;
        }

        auto const access = buffer.as<::Windows::Storage::Streams::IBufferByteAccess>();
        std::uint8_t* pixels{};
        if (FAILED(access->Buffer(&pixels)) || !pixels)
        {
            ClearImage();
            return;
        }

        std::memcpy(pixels, frame.Bgra.data(), frame.Bgra.size());
        m_bitmap.Invalidate();

        if (!m_hasImage)
        {
            m_hasImage = true;
            Raise(L"ImageVisibility");
        }
    }

    void ScrubPreviewViewModel::ClearImage()
    {
        if (!m_hasImage)
        {
            return;
        }
        m_hasImage = false;
        Raise(L"ImageVisibility");
    }

    void ScrubPreviewViewModel::SetOpen(bool open)
    {
        if (open == m_open)
        {
            return;
        }
        m_open = open;
        Raise(L"PreviewVisibility");
        if (m_skeleton)
        {
            Raise(L"SkeletonActive");
        }
    }

    void ScrubPreviewViewModel::UpdateLoading()
    {
        if (!::HaloDesktop::Playback::IsScrubPreviewLoading(m_requestId, m_answeredId))
        {
            if (m_skeletonTimer)
            {
                m_skeletonTimer.Stop();
            }
            SetSkeleton(false);
            return;
        }

        // A newer request carries on the wait already being counted instead of
        // restarting it, so a steady drag across a slow stream still reaches the skeleton.
        if (m_skeleton || !m_skeletonTimer || m_skeletonTimer.IsRunning())
        {
            return;
        }
        m_skeletonTimer.Start();
    }

    void ScrubPreviewViewModel::SetSkeleton(bool shown)
    {
        if (shown == m_skeleton)
        {
            return;
        }
        m_skeleton = shown;
        Raise(L"SkeletonVisibility");
        Raise(L"SkeletonActive");
        if (m_hasImage)
        {
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
