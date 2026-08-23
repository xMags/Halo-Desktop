#pragma once

#include <cstdint>
#include <winrt/Microsoft.UI.Xaml.h>

namespace HaloDesktop::Services
{
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
        ThemeService();

        [[nodiscard]] ThemePreference Preference() const noexcept;
        void SetPreference(ThemePreference preference);

        void Attach(winrt::Microsoft::UI::Xaml::FrameworkElement const& root);
        void Detach() noexcept;

    private:
        void Apply() const;

        ThemePreference m_preference{ ThemePreference::System };
        winrt::Microsoft::UI::Xaml::FrameworkElement m_root{ nullptr };
    };
}
