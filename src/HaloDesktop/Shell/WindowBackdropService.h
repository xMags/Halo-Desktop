#pragma once

#include "Shell/WindowBackdropPolicy.h"

#include <memory>
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Services
{
    class DevicePreferencesStore;
}

namespace HaloDesktop::Shell
{
    // The window's translucent ground: Windows' desktop acrylic, tinted with the
    // palette's own ground colour of whichever theme the window is drawn in, so
    // the window keeps Halo's colour with a soft blur of the desktop behind it.
    // Without acrylic (the setting is off, Windows cannot draw it, or attaching
    // it failed) the window paints its opaque ground as before. Windows itself
    // shows the backdrop's fallback colour, that same ground colour, when
    // transparency effects are off, on battery saver and over Remote Desktop.
    //
    // UI-thread-only. Attached by the main window, which keeps owning the
    // window, the theme scope and the ground element; Detach releases the
    // controller and must run before the window's content is torn down.
    class WindowBackdropService final
    {
    public:
        explicit WindowBackdropService(std::shared_ptr<Services::DevicePreferencesStore> preferences);
        ~WindowBackdropService();

        WindowBackdropService(WindowBackdropService const&) = delete;
        WindowBackdropService& operator=(WindowBackdropService const&) = delete;

        // The viewer's "Translucent window" setting on this PC.
        [[nodiscard]] bool Enabled() const noexcept;
        // Stored, then applied at once. A setting that cannot be stored still
        // applies for this session.
        void SetEnabled(bool enabled);
        // Windows can draw desktop acrylic here at all.
        [[nodiscard]] static bool IsSupported() noexcept;

        // `themeScope` is the element whose actual theme decides the tint (the
        // window's root, which a title page forces dark); `ground` is the
        // opaque ground, hidden while acrylic is drawn.
        void Attach(
            winrt::Microsoft::UI::Xaml::Window const& window,
            winrt::Microsoft::UI::Xaml::FrameworkElement const& themeScope,
            winrt::Microsoft::UI::Xaml::UIElement const& ground);
        void Detach() noexcept;

        void SetWindowActive(bool active);
        void SetPlayerShowing(bool showing);
        // The theme scope's actual theme changed: re-tint.
        void RefreshTheme();

    private:
        void Apply();
        void ApplyTint();
        void ShowGround(bool visible) noexcept;
        void ReleaseController() noexcept;

        std::shared_ptr<Services::DevicePreferencesStore> m_preferences;
        bool m_enabled{ true };
        bool m_windowActive{ true };
        bool m_playerShowing{};
        // Attaching acrylic failed once: the window keeps its opaque ground for
        // the rest of the session rather than retrying on every change.
        bool m_failed{};
        winrt::Microsoft::UI::Xaml::Window m_window{ nullptr };
        winrt::Microsoft::UI::Xaml::FrameworkElement m_themeScope{ nullptr };
        winrt::Microsoft::UI::Xaml::UIElement m_ground{ nullptr };
        winrt::Microsoft::UI::Composition::SystemBackdrops::DesktopAcrylicController m_controller{ nullptr };
        winrt::Microsoft::UI::Composition::SystemBackdrops::SystemBackdropConfiguration m_configuration{ nullptr };
    };
}
