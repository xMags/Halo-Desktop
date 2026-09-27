#pragma once

#include "Playback/IScrubPreviewSource.h"
#include "ScrubPreviewViewModel.g.h"

#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace winrt::HaloDesktop::implementation
{
    // The thumbnail card that follows the pointer along the seek bar. It owns the
    // decoded picture and the card's placement; the timeline arithmetic itself lives in
    // ScrubPreviewPolicy so it can be tested without a player.
    //
    // Frames arrive from a decoder thread and are marshalled onto the UI thread by the
    // preview source, so everything here runs on the UI thread.
    struct ScrubPreviewViewModel : ScrubPreviewViewModelT<ScrubPreviewViewModel>
    {
        explicit ScrubPreviewViewModel(
            std::shared_ptr<::HaloDesktop::Playback::IScrubPreviewSource> source);
        ~ScrubPreviewViewModel();

        void Activate();
        void Deactivate() noexcept;

        [[nodiscard]] Microsoft::UI::Xaml::Visibility PreviewVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap Image() const;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility ImageVisibility() const noexcept;
        [[nodiscard]] Microsoft::UI::Xaml::Visibility SkeletonVisibility() const noexcept;
        // True while the skeleton is on screen, so its animations run only while seen.
        [[nodiscard]] bool SkeletonActive() const noexcept;
        [[nodiscard]] winrt::hstring TimeText() const;
        [[nodiscard]] double OffsetX() const noexcept;
        [[nodiscard]] double CardWidth() const noexcept;
        [[nodiscard]] double CardHeight() const noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        // pointerX and trackWidth are measured against the seek slider itself, because
        // that is the element the pointer events are reported against.
        void Hover(double pointerX, double trackWidth, double durationSeconds);
        void Hide();
        void Reset();
        winrt::event_token PropertyChanged(
            Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler);
        void PropertyChanged(winrt::event_token const& token) noexcept;

    private:
        void OnFrame(::HaloDesktop::Playback::ScrubPreviewFrame const& frame);
        void ShowFrame(::HaloDesktop::Playback::ScrubPreviewFrame const& frame);
        void ClearImage();
        void SetOpen(bool open);
        // Settles the skeleton against the latest request: counts down to it while that
        // request waits, and drops it the moment the request is answered.
        void UpdateLoading();
        void SetSkeleton(bool shown);
        void Raise(wchar_t const* propertyName);

        std::shared_ptr<::HaloDesktop::Playback::IScrubPreviewSource> m_source;
        Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap m_bitmap{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_skeletonTimer{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker m_skeletonTickRevoker{};
        winrt::hstring m_timeText;
        std::uint64_t m_requestId{};
        // The newest request the source has answered, with a picture or without one.
        std::uint64_t m_answeredId{};
        double m_offsetX{};
        bool m_open{};
        bool m_hasImage{};
        bool m_skeleton{};
        winrt::event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_propertyChanged;
    };
}
