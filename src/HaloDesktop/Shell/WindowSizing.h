#pragma once

#include <cstdint>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Shell
{
    class WindowSizing final
    {
    public:
        explicit WindowSizing(winrt::Microsoft::UI::Xaml::Window const& window);

        [[nodiscard]] winrt::Microsoft::UI::Windowing::AppWindow const& AppWindow() const noexcept;
        [[nodiscard]] std::uintptr_t WindowHandle() const noexcept;

    private:
        winrt::Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
        std::uintptr_t m_windowHandle{};
    };
} // namespace HaloDesktop::Shell
