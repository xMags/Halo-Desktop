#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Shell
{
    // Keeps the main window hidden from the screen until XAML has drawn it.
    //
    // A WinUI window is shown the moment it is activated, before its content
    // has rendered, so the first thing on screen is an empty frame with
    // nothing to move, minimise or close. The window is cloaked from the
    // desktop compositor instead: it is created, activated and laid out as
    // usual, and composed, but not displayed until the root has loaded and
    // XAML has rendered after it. A content that never reports in still gets
    // its window after the fallback, so the app can never start invisible.
    //
    // UI-thread-only. Begin before the window is activated; the reveal runs
    // once, and destroying the object reveals the window if it has not been.
    class WindowReveal final : public std::enable_shared_from_this<WindowReveal>
    {
        struct ConstructionKey final
        {
            explicit ConstructionKey() = default;
        };

    public:
        static constexpr std::chrono::seconds Fallback{ 4 };

        [[nodiscard]] static std::shared_ptr<WindowReveal> Begin(
            std::uintptr_t windowHandle,
            winrt::Microsoft::UI::Xaml::FrameworkElement const& root,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher);

        // Reachable only through Begin, which owns the shared lifetime the
        // event handlers' weak references rely on.
        WindowReveal(ConstructionKey, std::uintptr_t windowHandle);
        WindowReveal(WindowReveal const&) = delete;
        WindowReveal& operator=(WindowReveal const&) = delete;
        ~WindowReveal();

        void Reveal() noexcept;

    private:
        void Attach(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& root,
            winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher);
        void WatchRendering();
        void OnRendering() noexcept;

        std::uintptr_t m_windowHandle{};
        bool m_cloaked{};
        bool m_revealed{};
        bool m_watchingRendering{};
        int m_renderedFrames{};
        winrt::Microsoft::UI::Xaml::FrameworkElement::Loaded_revoker m_loadedRevoker{};
        winrt::event_token m_renderingToken{};
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_fallbackTimer{ nullptr };
    };
}
