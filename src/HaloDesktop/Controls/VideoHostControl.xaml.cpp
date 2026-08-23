#include "pch.h"
#include "Controls/VideoHostControl.xaml.h"
#if __has_include("VideoHostControl.g.cpp")
#include "VideoHostControl.g.cpp"
#endif

#include "App.xaml.h"
#include "Playback/IPlaybackEngine.h"
#include "Shell/WindowPresentationService.h"

#include <cmath>
#include <mutex>
#include <stdexcept>
#include <wil/resource.h>

namespace
{
    constexpr wchar_t VideoHostClassName[] = L"HaloDesktop.MpvVideoHost";

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
        try
        {
            App::Services().Playback->AttachVideoWindow(m_hostWindow);
        }
        catch (...)
        {
            DestroyWindow(window);
            m_hostWindow = 0;
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
        static_cast<void>(DestroyWindow(window));
    }

    std::uintptr_t VideoHostControl::HostWindowHandle() const noexcept
    {
        return m_hostWindow;
    }

    void VideoHostControl::UpdateBounds()
    {
        if (m_hostWindow == 0 || ActualWidth() <= 0.0 || ActualHeight() <= 0.0)
        {
            return;
        }

        auto const root = XamlRoot();
        if (!root)
        {
            return;
        }
        auto const origin = TransformToVisual(nullptr).TransformPoint({ 0.0f, 0.0f });
        auto const scale = root.RasterizationScale();
        auto const x = static_cast<int>(std::lround(origin.X * scale));
        auto const y = static_cast<int>(std::lround(origin.Y * scale));
        auto const width = (std::max)(1, static_cast<int>(std::lround(ActualWidth() * scale)));
        auto const height = (std::max)(1, static_cast<int>(std::lround(ActualHeight() * scale)));
        winrt::check_bool(SetWindowPos(reinterpret_cast<HWND>(m_hostWindow), nullptr, x, y, width, height,
                                       SWP_NOACTIVATE | SWP_NOZORDER));
    }
} // namespace winrt::HaloDesktop::implementation
