#pragma once

#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Shell
{
    class WindowSizing final
    {
    public:
        explicit WindowSizing(winrt::Microsoft::UI::Xaml::Window const& window);

        [[nodiscard]] winrt::Microsoft::UI::Windowing::AppWindow const& AppWindow() const noexcept;

    private:
        winrt::Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
    };
}
