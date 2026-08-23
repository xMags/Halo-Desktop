#pragma once

#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Shell
{
    // UI-thread-only adapter for window presentation. The service does not own
    // the AppWindow or title row and must be detached before XAML teardown.
    class WindowPresentationService final
    {
    public:
        void Attach(winrt::Microsoft::UI::Windowing::AppWindow const& appWindow,
                    winrt::Microsoft::UI::Xaml::Controls::RowDefinition const& titleBarRow);
        void Detach() noexcept;
        void SetFullscreen(bool fullscreen);
        [[nodiscard]] bool IsFullscreen() const noexcept;

    private:
        winrt::Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::RowDefinition m_titleBarRow{ nullptr };
        bool m_fullscreen{};
        bool m_wasMaximized{};
    };
} // namespace HaloDesktop::Shell
