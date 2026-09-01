#include "pch.h"
#include "Controls/VideoHostControl.xaml.h"
#if __has_include("VideoHostControl.g.cpp")
#include "VideoHostControl.g.cpp"
#endif

#include "App.xaml.h"
#include "Playback/IPlaybackEngine.h"

#include <microsoft.ui.xaml.media.dxinterop.h>

#include <algorithm>
#include <cmath>

namespace
{
    // Upper bound on a swapchain edge. D3D11 refuses anything larger, and a
    // layout glitch must not turn into a gigantic allocation.
    constexpr double MaximumSurfaceEdge = 16384.0;

    [[nodiscard]] std::uint32_t ToSurfaceEdge(double logical, float scale) noexcept
    {
        auto const pixels = std::isfinite(logical) && std::isfinite(scale) ? logical * scale : 0.0;
        auto const clamped = std::clamp(std::round(pixels), 1.0, MaximumSurfaceEdge);
        return static_cast<std::uint32_t>(clamped);
    }
} // namespace

namespace winrt::HaloDesktop::implementation
{
    VideoHostControl::VideoHostControl() = default;

    VideoHostControl::~VideoHostControl()
    {
        ReleaseSurface();
    }

    void VideoHostControl::OnLoaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                                    [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        EnsureSurface();
        UpdateSurfaceSize();
    }

    void VideoHostControl::OnUnloaded([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                                      [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        ReleaseSurface();
    }

    void VideoHostControl::OnSizeChanged([[maybe_unused]] winrt::Windows::Foundation::IInspectable const& sender,
                                         [[maybe_unused]] Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        UpdateSurfaceSize();
    }

    void VideoHostControl::OnCompositionScaleChanged(
        [[maybe_unused]] Microsoft::UI::Xaml::Controls::SwapChainPanel const& sender,
        [[maybe_unused]] winrt::Windows::Foundation::IInspectable const& args)
    {
        UpdateSurfaceSize();
        try
        {
            ApplyScaleTransform();
        }
        catch (...)
        {
        }
    }

    void VideoHostControl::EnsureSurface()
    {
        if (m_surfaceAttached)
        {
            return;
        }

        App::Services().Playback->AttachVideoSurface(
            CurrentSurfaceSize(),
            [weak = get_weak()](std::uintptr_t address)
            {
                if (auto const self = weak.get())
                {
                    self->ApplySwapChain(address);
                }
            });
        m_surfaceAttached = true;
    }

    void VideoHostControl::ReleaseSurface() noexcept
    {
        if (!m_surfaceAttached)
        {
            return;
        }
        m_surfaceAttached = false;
        try
        {
            // Stop publishes a null swapchain through the handler before libmpv
            // tears the swapchain down, so the panel never presents a dead one.
            App::Services().Playback->Stop();
            App::Services().Playback->DetachVideoSurface();
        }
        catch (...)
        {
        }
        ApplySwapChain(0);
    }

    ::HaloDesktop::Playback::VideoSurfaceSize VideoHostControl::CurrentSurfaceSize()
    {
        auto const panel = VideoPanel();
        return {
            .WidthPixels = ToSurfaceEdge(panel.ActualWidth(), panel.CompositionScaleX()),
            .HeightPixels = ToSurfaceEdge(panel.ActualHeight(), panel.CompositionScaleY()),
        };
    }

    void VideoHostControl::UpdateSurfaceSize() noexcept
    {
        if (!m_surfaceAttached)
        {
            return;
        }
        try
        {
            App::Services().Playback->SetVideoSurfaceSize(CurrentSurfaceSize());
        }
        catch (...)
        {
        }
    }

    void VideoHostControl::ApplySwapChain(std::uintptr_t address) noexcept
    {
        try
        {
            auto const native = VideoPanel().as<ISwapChainPanelNative>();
            if (address == 0)
            {
                m_swapChain = nullptr;
                winrt::check_hresult(native->SetSwapChain(nullptr));
                return;
            }

            // libmpv publishes its swapchain as an integer address, and this is
            // the only place that address becomes a pointer again. libmpv keeps
            // ownership, so copy_from takes a reference of our own rather than
            // adopting the one libmpv holds.
            auto* const raw = reinterpret_cast<IDXGISwapChain*>(address);
            winrt::com_ptr<IDXGISwapChain> swapChain;
            swapChain.copy_from(raw);
            m_swapChain = swapChain.try_as<IDXGISwapChain2>();
            ApplyScaleTransform();
            winrt::check_hresult(native->SetSwapChain(swapChain.get()));
        }
        catch (...)
        {
            m_swapChain = nullptr;
        }
    }

    void VideoHostControl::ApplyScaleTransform()
    {
        if (!m_swapChain)
        {
            return;
        }

        // The swapchain is sized in physical pixels. XAML positions it in
        // logical pixels, so the inverse composition scale maps one onto the
        // other; without it a 150% display shows the video 1.5 times too large.
        auto const panel = VideoPanel();
        auto const scaleX = panel.CompositionScaleX();
        auto const scaleY = panel.CompositionScaleY();
        if (scaleX <= 0.0f || scaleY <= 0.0f)
        {
            return;
        }
        DXGI_MATRIX_3X2_F transform{};
        transform._11 = 1.0f / scaleX;
        transform._22 = 1.0f / scaleY;
        winrt::check_hresult(m_swapChain->SetMatrixTransform(&transform));
    }
} // namespace winrt::HaloDesktop::implementation
