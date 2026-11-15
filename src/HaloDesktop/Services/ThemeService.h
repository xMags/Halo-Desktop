#pragma once

#include <cstdint>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Services
{
    class DevicePreferencesStore;

    enum class ThemePreference : std::int32_t
    {
        Light = 0,
        Dark = 1,
        System = 2,
    };

    // UI-thread-only. Application.RequestedTheme is fixed once the app starts, so the
    // preference is applied to the window's root element instead, which re-resolves the
    // theme dictionaries for everything below it. The service does not own that element
    // and must be detached before XAML teardown.
    class ThemeService final
    {
    public:
        explicit ThemeService(std::shared_ptr<DevicePreferencesStore> preferences);

        // The viewer's saved choice. A forced dark scope never changes it.
        [[nodiscard]] ThemePreference Preference() const noexcept;
        void SetPreference(ThemePreference preference);

        // While the scope is active the whole window paints the dark palette,
        // whatever the preference says; ending it puts the preference back. The
        // shell holds it while a title page is on screen, because a title page is
        // drawn on its art in both themes. Nothing is stored: a launch always
        // starts outside the scope.
        void SetForcedDarkScope(bool active);
        [[nodiscard]] bool IsForcedDarkScopeActive() const noexcept;

        void Attach(winrt::Microsoft::UI::Xaml::FrameworkElement const& root);
        void Detach() noexcept;

    private:
        void Apply() const;

        bool m_forcedDark{};
        ThemePreference m_preference{ ThemePreference::System };
        std::shared_ptr<DevicePreferencesStore> m_preferences;
        winrt::Microsoft::UI::Xaml::FrameworkElement m_root{ nullptr };
    };
}
