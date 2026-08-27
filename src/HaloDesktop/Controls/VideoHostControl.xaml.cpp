#include "pch.h"
#include "Controls/VideoHostControl.xaml.h"
#if __has_include("VideoHostControl.g.cpp")
#include "VideoHostControl.g.cpp"
#endif

#include "App.xaml.h"
#include "Playback/IPlaybackEngine.h"
#include "Shell/WindowPresentationService.h"

#include <cmath>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <wil/resource.h>

namespace
{
    constexpr wchar_t VideoHostClassName[] = L"HaloDesktop.MpvVideoHost";

    [[nodiscard]] std::optional<int> TryRoundToInt(double value) noexcept
    {
        constexpr auto minimum = static_cast<double>((std::numeric_limits<int>::min)());
        constexpr auto maximum = static_cast<double>((std::numeric_limits<int>::max)());
        if (!std::isfinite(value) || value < minimum || value > maximum)
        {
            return std::nullopt;
        }
        return static_cast<int>(std::lround(value));
    }

    LRESULT CALLBACK VideoHostWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
    {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void EnsureVideoHostClass()
    {
        static std::once_flag registrationFlag;
        std::call_once(registrationFlag, [] {
            static wil::unique_hbrush const backgroundBrush{ CreateSolidBrush(RGB(0, 0, 0)) };
            if (!backgroundBrush)
            {
                throw std::runtime_error("Unable to create the video host background brush");
            }

            WNDCLASSEXW windowClass{};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = VideoHostWindowProcedure;
            windowClass.hInstance = GetModuleHandleW(nullptr);
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = backgroundBrush.get();
            windowClass.lpszClassName = VideoHostClassName;
            if (RegisterClassExW(&windowClass) == 0)
            {
                winrt::throw_last_error();
            }
        });
    }
} // namespace

namespace winrt::HaloDesktop::implementation
{
    VideoHostControl::VideoHostControl() = default;

    VideoHostControl::~VideoHostControl()
    {
        DestroyHostWindow();
    }

    void VideoHostControl::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                                    [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        EnsureHostWindow();
        if (auto const root = XamlRoot())
        {
            m_xamlRootChangedRevoker = root.Changed(
                winrt::auto_revoke,
                [weak = get_weak()]([[maybe_unused]] Microsoft::UI::Xaml::XamlRoot const& changedRoot,
                                    [[maybe_unused]] Microsoft::UI::Xaml::XamlRootChangedEventArgs const& changedArgs) {
                    if (auto const self = weak.get())
                    {
                        self->UpdateBounds();
                    }
                });
        }
        UpdateBounds();
    }

    void VideoHostControl::OnUnloaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                                      [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        DestroyHostWindow();
    }

    void VideoHostControl::OnSizeChanged([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                                         [[maybe_unused]] Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        UpdateBounds();
    }

    void VideoHostControl::EnsureHostWindow()
    {
        if (m_hostWindow != 0)
        {
            return;
        }

        EnsureVideoHostClass();
        auto const parentValue = App::Services().WindowPresentation->WindowHandle();
        auto const parent = reinterpret_cast<HWND>(parentValue);
        auto const instance = GetModuleHandleW(nullptr);
        auto const window = CreateWindowExW(0, VideoHostClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0,
                                            1, 1, parent, nullptr, instance, nullptr);
        if (!window)
        {
            winrt::throw_last_error();
        }

        // HWND is an opaque pointer. uintptr_t keeps that Windows type out of
        // the platform-neutral playback engine contract.
        m_hostWindow = reinterpret_cast<std::uintptr_t>(window);
        ++m_hostWindowGeneration;
        m_lastBounds.reset();
        m_boundsRetryQueued = false;
        try
        {
            App::Services().Playback->AttachVideoWindow(m_hostWindow);
        }
        catch (...)
        {
            DestroyWindow(window);
            m_hostWindow = 0;
            ++m_hostWindowGeneration;
            m_lastBounds.reset();
            m_boundsRetryQueued = false;
            throw;
        }
    }

    void VideoHostControl::DestroyHostWindow() noexcept
    {
        try
        {
            m_xamlRootChangedRevoker.revoke();
        }
        catch (...)
        {
        }
        ++m_hostWindowGeneration;
        m_lastBounds.reset();
        m_boundsRetryQueued = false;
        if (m_hostWindow == 0)
        {
            return;
        }

        auto const window = reinterpret_cast<HWND>(m_hostWindow);
        m_hostWindow = 0;
        try
        {
            App::Services().Playback->Stop();
            App::Services().Playback->DetachVideoWindow();
        }
        catch (...)
        {
        }
        if (IsWindow(window))
        {
            static_cast<void>(DestroyWindow(window));
        }
    }

    std::uintptr_t VideoHostControl::HostWindowHandle() const noexcept
    {
        return m_hostWindow;
    }

    void VideoHostControl::UpdateBounds() noexcept
    {
        TryUpdateBounds(true);
    }

    void VideoHostControl::TryUpdateBounds(bool allowRetry) noexcept
    {
        if (m_hostWindow == 0)
        {
            return;
        }

        auto const window = reinterpret_cast<HWND>(m_hostWindow);
        if (!IsWindow(window))
        {
            return;
        }

        try
        {
            auto const actualWidth = ActualWidth();
            auto const actualHeight = ActualHeight();
            auto const root = XamlRoot();
            if (!root || !std::isfinite(actualWidth) || !std::isfinite(actualHeight)
                || actualWidth <= 0.0 || actualHeight <= 0.0)
            {
                if (allowRetry) QueueBoundsRetry();
                return;
            }

            auto const scale = root.RasterizationScale();
            if (!std::isfinite(scale) || scale <= 0.0)
            {
                if (allowRetry) QueueBoundsRetry();
                return;
            }

            auto const origin = TransformToVisual(nullptr).TransformPoint({ 0.0f, 0.0f });
            auto const x = TryRoundToInt(static_cast<double>(origin.X) * scale);
            auto const y = TryRoundToInt(static_cast<double>(origin.Y) * scale);
            auto const width = TryRoundToInt(actualWidth * scale);
            auto const height = TryRoundToInt(actualHeight * scale);
            if (!x || !y || !width || !height)
            {
                if (allowRetry) QueueBoundsRetry();
                return;
            }

            HostBounds const bounds{
                .X = *x,
                .Y = *y,
                .Width = (std::max)(1, *width),
                .Height = (std::max)(1, *height),
            };
            if (m_lastBounds && *m_lastBounds == bounds)
            {
                return;
            }

            if (!SetWindowPos(
                    window,
                    nullptr,
                    bounds.X,
                    bounds.Y,
                    bounds.Width,
                    bounds.Height,
                    SWP_NOACTIVATE | SWP_NOZORDER))
            {
                if (allowRetry) QueueBoundsRetry();
                return;
            }
            m_lastBounds = bounds;
        }
        catch (...)
        {
            if (allowRetry) QueueBoundsRetry();
        }
    }

    void VideoHostControl::QueueBoundsRetry() noexcept
    {
        if (m_boundsRetryQueued || m_hostWindow == 0)
        {
            return;
        }

        auto const generation = m_hostWindowGeneration;
        try
        {
            auto const dispatcher = DispatcherQueue();
            if (!dispatcher)
            {
                return;
            }

            m_boundsRetryQueued = true;
            if (dispatcher.TryEnqueue(
                Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
                [weak = get_weak(), generation]()
                {
                    auto const self = weak.get();
                    if (!self || generation != self->m_hostWindowGeneration)
                    {
                        return;
                    }
                    self->m_boundsRetryQueued = false;
                    self->TryUpdateBounds(false);
                }))
            {
                return;
            }
        }
        catch (...)
        {
        }
        m_boundsRetryQueued = false;
    }
} // namespace winrt::HaloDesktop::implementation
