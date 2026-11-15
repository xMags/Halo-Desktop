#include "pch.h"
#include "Shell/WindowReveal.h"

#include <dwmapi.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace
{
    // Rendering fires as XAML starts a frame, so the first tick after Loaded
    // is the frame being drawn, not one already presented. Waiting for the
    // second means one complete frame is on the swap chain before the window
    // is shown.
    constexpr int FramesBeforeReveal = 2;

    bool SetCloaked(std::uintptr_t windowHandle, bool cloaked) noexcept
    {
        // HWND is an opaque pointer supplied by WinUI; the integer form only
        // keeps the header free of windows.h.
        auto const window = reinterpret_cast<HWND>(windowHandle);
        BOOL const value = cloaked ? TRUE : FALSE;
        return SUCCEEDED(DwmSetWindowAttribute(window, DWMWA_CLOAK, &value, sizeof(value)));
    }
}

namespace HaloDesktop::Shell
{
    std::shared_ptr<WindowReveal> WindowReveal::Begin(
        std::uintptr_t windowHandle,
        winrt::Microsoft::UI::Xaml::FrameworkElement const& root,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher)
    {
        auto const reveal = std::make_shared<WindowReveal>(ConstructionKey{}, windowHandle);
        reveal->m_cloaked = windowHandle != 0 && SetCloaked(windowHandle, true);
        if (!reveal->m_cloaked)
        {
            // Nothing is hidden, so there is nothing to wait for.
            reveal->m_revealed = true;
            return reveal;
        }
        try
        {
            reveal->Attach(root, dispatcher);
        }
        catch (...)
        {
            reveal->Reveal();
        }
        return reveal;
    }

    WindowReveal::WindowReveal(ConstructionKey, std::uintptr_t windowHandle)
        : m_windowHandle(windowHandle)
    {
    }

    WindowReveal::~WindowReveal()
    {
        Reveal();
    }

    void WindowReveal::Attach(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& root,
        winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher)
    {
        std::weak_ptr<WindowReveal> weak = weak_from_this();

        m_fallbackTimer = dispatcher.CreateTimer();
        m_fallbackTimer.Interval(Fallback);
        m_fallbackTimer.IsRepeating(false);
        m_fallbackTimer.Tick([weak](auto const&, auto const&)
        {
            if (auto const self = weak.lock()) self->Reveal();
        });
        m_fallbackTimer.Start();

        if (!root)
        {
            // Only the fallback can reveal a window without content.
            return;
        }
        if (root.IsLoaded())
        {
            WatchRendering();
            return;
        }
        m_loadedRevoker = root.Loaded(winrt::auto_revoke, [weak](auto const&, auto const&)
        {
            auto const self = weak.lock();
            if (!self || self->m_revealed) return;
            try
            {
                self->WatchRendering();
            }
            catch (...)
            {
                self->Reveal();
            }
        });
    }

    void WindowReveal::WatchRendering()
    {
        if (m_watchingRendering || m_revealed) return;
        std::weak_ptr<WindowReveal> weak = weak_from_this();
        m_renderingToken = winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(
            [weak](auto const&, auto const&)
            {
                if (auto const self = weak.lock()) self->OnRendering();
            });
        m_watchingRendering = true;
    }

    void WindowReveal::OnRendering() noexcept
    {
        if (m_revealed) return;
        if (++m_renderedFrames >= FramesBeforeReveal) Reveal();
    }

    void WindowReveal::Reveal() noexcept
    {
        if (m_revealed) return;
        m_revealed = true;
        try
        {
            m_loadedRevoker.revoke();
            if (m_watchingRendering)
            {
                winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(m_renderingToken);
                m_watchingRendering = false;
            }
            if (m_fallbackTimer) m_fallbackTimer.Stop();
        }
        catch (...)
        {
        }
        if (m_cloaked)
        {
            static_cast<void>(SetCloaked(m_windowHandle, false));
            m_cloaked = false;
        }
    }
}
