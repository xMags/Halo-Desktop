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
        // Matches the icon resource declared in HaloDesktop.rc. Windows shows
        // the lowest-numbered group icon as the executable's icon, so both the
        // window and Explorer end up on the same artwork.
        static constexpr int ApplicationIconResourceId{ 1 };

        void ApplyWindowIcon() noexcept;

        winrt::Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
        std::uintptr_t m_windowHandle{};
    };
} // namespace HaloDesktop::Shell
